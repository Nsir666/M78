/**
 * @file    app_tasks.c
 * @brief   FreeRTOS任务实现 - 5个任务
 *          Task_Sensor   (优先级4) - 传感器采集
 *          Task_Display  (优先级3) - OLED显示
 *          Task_Alarm    (优先级3) - 报警管理
 *          Task_Key      (优先级2) - 按键处理
 *          Task_Bluetooth(优先级1) - 蓝牙通信
 */

#include "app_tasks.h"
#include "OLED_I2C.h"
#include "ds1302.h"
#include "ds18b20.h"
#include "adxl345.h"
#include "max30102_read.h"
#include "myiic.h"
#include "iic.h"
#include "key.h"
#include "usart1.h"
#include "stmflash.h"
#include "delay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*===========================================================================
 * 全局变量定义
 *===========================================================================*/
SensorData_t g_data;  /* 由g_dataMutex互斥锁保护，无需volatile */

/* 心率血氧算法输出(在max30102_read.c中定义) */
extern int32_t hrAvg;
extern int32_t spo2Avg;

SemaphoreHandle_t g_dataMutex  = NULL;
SemaphoreHandle_t g_oledMutex  = NULL;
SemaphoreHandle_t g_uartRxSem  = NULL;
QueueHandle_t     g_keyQueue   = NULL;

/*===========================================================================
 * Flash存储相关
 *===========================================================================*/
#define FLASH_SAVE_ADDR  ((u32)0x0800F000)

static void Store_Data(void)
{
    u16 DATA_BUF[12];
    DATA_BUF[0]  = g_data.steps;
    DATA_BUF[1]  = g_data.mileageSteps;
    DATA_BUF[2]  = g_data.stepLength;
    DATA_BUF[3]  = g_data.hrMin;
    DATA_BUF[4]  = g_data.hrMax;
    DATA_BUF[5]  = g_data.spo2Min;
    DATA_BUF[6]  = g_data.tempMin;
    DATA_BUF[7]  = g_data.tempMax;
    DATA_BUF[8]  = (g_data.warnMileage >> 16) & 0xFFFF;
    DATA_BUF[9]  = (g_data.warnMileage >> 0)  & 0xFFFF;
    DATA_BUF[10] = (g_data.alarmHour << 8) | g_data.alarmMin;
    DATA_BUF[11] = g_data.alarmEnabled;
    STMFLASH_Write(FLASH_SAVE_ADDR + 0x20, (u16*)DATA_BUF, 12);
}

static void Read_Data(void)
{
    u16 DATA_BUF[12];
    STM32F10x_Read(FLASH_SAVE_ADDR + 0x20, (u16*)DATA_BUF, 12);
    g_data.steps         = DATA_BUF[0];
    g_data.mileageSteps  = DATA_BUF[1];
    g_data.stepLength    = DATA_BUF[2];
    g_data.hrMin         = DATA_BUF[3];
    g_data.hrMax         = DATA_BUF[4];
    g_data.spo2Min       = DATA_BUF[5];
    g_data.tempMin       = DATA_BUF[6];
    g_data.tempMax       = DATA_BUF[7];
    g_data.warnMileage   = ((long)DATA_BUF[8] << 16) | DATA_BUF[9];
    g_data.alarmHour     = (DATA_BUF[10] >> 8) & 0xFF;
    g_data.alarmMin      = DATA_BUF[10] & 0xFF;
    g_data.alarmEnabled  = DATA_BUF[11] & 0x01;
}

static void CheckNewMcu(void)
{
    u8 comper_str[6];
    STM32F10x_Read(FLASH_SAVE_ADDR, (u16*)comper_str, 5);
    comper_str[5] = '\0';
    if (strstr((char*)comper_str, "FDYDZ") == NULL)
    {
        STMFLASH_Write(FLASH_SAVE_ADDR, (u16*)"FDYDZ", 5);
        DelayMs(50);
        Store_Data();
        DelayMs(50);
    }
    Read_Data();
}

/*===========================================================================
 * 任务创建函数
 *===========================================================================*/
void AppTaskCreate(void)
{
    /* 创建互斥锁 */
    g_dataMutex = xSemaphoreCreateMutex();
    g_oledMutex = xSemaphoreCreateMutex();

    /* 创建二值信号量(UART接收) */
    g_uartRxSem = xSemaphoreCreateBinary();

    /* 创建按键队列 */
    g_keyQueue = xQueueCreate(8, sizeof(uint8_t));

    /* 创建5个任务 */
    xTaskCreate(Task_Sensor,    "Sensor",   128, NULL, 4, NULL);  /* 512B */
    xTaskCreate(Task_Display,   "Display",  128, NULL, 3, NULL);  /* 512B */
    xTaskCreate(Task_Alarm,     "Alarm",    64,  NULL, 3, NULL);  /* 256B */
    xTaskCreate(Task_Key,       "Key",      64,  NULL, 2, NULL);  /* 256B */
    xTaskCreate(Task_Bluetooth, "Ble",      64,  NULL, 1, NULL);  /* 256B */
}

/*===========================================================================
 * Task_Sensor - 传感器采集任务 (优先级4, 最高)
 * 周期: 200ms
 * 功能: 读取DS1302/DS18B20/ADXL345/MAX30102
 *===========================================================================*/
void Task_Sensor(void *pvParameters)
{
    float adx, ady, adz;
    float acc;
    static u8 flag = 0;
    static u16 lastSteps = 0;

    (void)pvParameters;

    /* 初始化外设 */
    DS18B20_Init();
    DS1302_Init(&g_data.dateTime);
    IIC_init();
    adxl345_init();
    Init_MAX30102();

    /* 从Flash读取保存的数据 */
    CheckNewMcu();

    /* 初始化默认值(如果Flash中没有) */
    if (g_data.stepLength == 0) g_data.stepLength = 40;
    if (g_data.hrMin == 0)      g_data.hrMin = 60;
    if (g_data.hrMax == 0)      g_data.hrMax = 120;
    if (g_data.spo2Min == 0)    g_data.spo2Min = 80;
    if (g_data.tempMax == 0)    g_data.tempMax = 373;
    if (g_data.tempMin == 0)    g_data.tempMin = 150;
    if (g_data.warnMileage == 0) g_data.warnMileage = 1000;

    for (;;)
    {
        if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            /* 1. 读取DS1302时间 */
            DS1302_DateRead((DATE*)&g_data.dateTime);

            /* 2. 读取DS18B20温度 */
            g_data.temperature = DS18B20_Get_Temp();

            /* 3. 读取ADXL345加速度 → 计步 */
            adxl345_read_average(&adx, &ady, &adz, 10);
            acc = ady;
            if (acc > 0)
            {
                if (acc / 10 >= 4 && flag == 1)
                {
                    flag = 0;
                    if (g_data.steps < 60000) g_data.steps++;
                    if (g_data.mileageSteps < 60000) g_data.mileageSteps++;
                    if (lastSteps != g_data.steps)
                    {
                        lastSteps = g_data.steps;
                        Store_Data();
                    }
                }
            }
            if (acc < 0)
            {
                acc = -acc;
                if (acc / 10 >= 4) flag = 1;
            }
            g_data.mileage = (g_data.mileageSteps * g_data.stepLength) / 100;

            /* 4. 读取MAX30102心率血氧 */
            ReadHeartRateSpO2();
            g_data.heartRate = hrAvg;
            g_data.spo2      = spo2Avg;

            xSemaphoreGive(g_dataMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*===========================================================================
 * 内部辅助函数 - 显示相关
 *===========================================================================*/
static void OLED_ShowCentigrade(u8 x, u8 y)
{
    /* 显示°C符号 */
    OLED_ShowChar(x, y, 'C', 2, 0);
}

/* 中文字模索引映射(沿用原项目的codetab.h) */
/* 原项目中中文显示通过OLED_ShowCN(index, x, y, mode)调用 */
/* index对应codetab.h中的中文字模偏移 */

/*===========================================================================
 * Task_Display - 显示任务 (优先级3)
 * 周期: 100ms
 * 功能: 根据page显示不同页面
 *===========================================================================*/
void Task_Display(void *pvParameters)
{
    u8 i, x;
    u8 shanshuo = 0;
    char display[16];
    float tempMileage;

    (void)pvParameters;

    /* 初始显示 - 欢迎界面 */
    if (xSemaphoreTake(g_oledMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        OLED_CLS();
        for (i = 0; i < 8; i++) OLED_ShowCN(i * 16, 2, i + 8, 0);  /* 欢迎使用智能手环 */
        xSemaphoreGive(g_oledMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 初始显示标签 */
    if (xSemaphoreTake(g_oledMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        OLED_CLS();
        if (g_data.page == 0)
        {
            for (i = 0; i < 2; i++) OLED_ShowCN(i * 16,      4, i + 16, 1); /* 心率 */
            for (i = 0; i < 2; i++) OLED_ShowCN(i * 16 + 48,  4, i + 18, 1); /* 血氧 */
            for (i = 0; i < 2; i++) OLED_ShowCN(i * 16 + 95,  4, i + 20, 1); /* 体温 */
            OLED_ShowCentigrade(112, 2);
        }
        else
        {
            for (i = 0; i < 2; i++) OLED_ShowCN(i * 16, 2, i + 44, 0); /* 计时 */
            OLED_ShowChar(32, 2, ':', 2, 0);
        }
        xSemaphoreGive(g_oledMutex);
    }

    for (;;)
    {
        shanshuo = !shanshuo;

        if (xSemaphoreTake(g_oledMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                if (g_data.page == 0)
                {
                    /*=== Page 0: 健康监测页面 ===*/

                    /* 显示日期 */
                    if (g_data.setn == 0)
                    {
                        x = 0;
                        OLED_ShowChar((x++) * 8, 0, '2', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, g_data.dateTime.year / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, g_data.dateTime.year % 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, '-', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, g_data.dateTime.mon / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, g_data.dateTime.mon % 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, '-', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, g_data.dateTime.day / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 0, g_data.dateTime.day % 10 + '0', 2, 0);

                        /* 显示时间 */
                        x = 0;
                        OLED_ShowChar((x++) * 8, 2, g_data.dateTime.hour / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, g_data.dateTime.hour % 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, ':', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, g_data.dateTime.min / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, g_data.dateTime.min % 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, ':', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, g_data.dateTime.sec / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 2, g_data.dateTime.sec % 10 + '0', 2, 0);

                        /* 显示温度 */
                        x = 10;
                        if ((g_data.temperature <= g_data.tempMin || g_data.temperature >= g_data.tempMax) && shanshuo)
                        {
                            OLED_ShowStr((x) * 8, 2, (u8*)"    ", 2);
                        }
                        else
                        {
                            OLED_ShowChar((x++) * 8, 2, g_data.temperature / 100 + '0', 2, 0);
                            OLED_ShowChar((x++) * 8, 2, g_data.temperature % 100 / 10 + '0', 2, 0);
                            OLED_ShowChar((x++) * 8, 2, '.', 2, 0);
                            OLED_ShowChar((x++) * 8, 2, g_data.temperature % 10 + '0', 2, 0);
                        }
                    }

                    /* 显示心率 */
                    x = 0;
                    if (g_data.heartRate != 0 && (g_data.heartRate >= g_data.hrMax || g_data.heartRate <= g_data.hrMin) && shanshuo)
                    {
                        OLED_ShowStr(0, 6, (u8*)"   ", 2);
                    }
                    else
                    {
                        OLED_ShowChar((x++) * 8, 6, g_data.heartRate % 1000 / 100 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.heartRate % 100 / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.heartRate % 10 + '0', 2, 0);
                    }

                    /* 显示血氧 */
                    x = 6;
                    if (g_data.spo2 != 0 && g_data.spo2 <= g_data.spo2Min && shanshuo)
                    {
                        OLED_ShowStr(48, 6, (u8*)"   ", 2);
                    }
                    else
                    {
                        OLED_ShowChar((x++) * 8, 6, g_data.spo2 % 1000 / 100 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.spo2 % 100 / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.spo2 % 10 + '0', 2, 0);
                    }

                    /* 显示步数 */
                    x = 11;
                    if (g_data.steps > 9999)
                    {
                        OLED_ShowChar((x++) * 8, 6, g_data.steps / 10000 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 10000 / 1000 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 1000 / 100 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 100 / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 10 + '0', 2, 0);
                    }
                    else if (g_data.steps > 999)
                    {
                        OLED_ShowChar((x++) * 8, 6, ' ', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 10000 / 1000 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 1000 / 100 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 100 / 10 + '0', 2, 0);
                        OLED_ShowChar((x++) * 8, 6, g_data.steps % 10 + '0', 2, 0);
                    }
                    else if (g_data.steps > 99)
                    {
                        OLED_ShowStr(88, 6, (u8*)"  ", 2);
                        OLED_ShowChar((x + 2) * 8, 6, g_data.steps % 1000 / 100 + '0', 2, 0);
                        OLED_ShowChar((x + 3) * 8, 6, g_data.steps % 100 / 10 + '0', 2, 0);
                        OLED_ShowChar((x + 4) * 8, 6, g_data.steps % 10 + '0', 2, 0);
                    }
                    else
                    {
                        sprintf(display, "%5d", g_data.steps);
                        OLED_ShowStr(88, 6, (u8*)display, 2);
                    }
                }
                else
                {
                    /*=== Page 1: 运动页面 ===*/

                    /* 显示秒表时间 */
                    if (g_data.setn == 0)
                    {
                        OLED_ShowChar(32, 0, g_data.timeCount / 3600 / 10 + '0', 2, 0);
                        OLED_ShowChar(40, 0, g_data.timeCount / 3600 % 10 + '0', 2, 0);
                        OLED_ShowChar(48, 0, ':', 2, 0);
                        OLED_ShowChar(56, 0, g_data.timeCount % 3600 / 60 / 10 + '0', 2, 0);
                        OLED_ShowChar(64, 0, g_data.timeCount % 3600 / 60 % 10 + '0', 2, 0);
                        OLED_ShowChar(72, 0, ':', 2, 0);
                        OLED_ShowChar(80, 0, g_data.timeCount % 60 / 10 + '0', 2, 0);
                        OLED_ShowChar(88, 0, g_data.timeCount % 60 % 10 + '0', 2, 0);
                    }

                    /* 显示里程 */
                    tempMileage = (float)g_data.mileage / 1000;
                    sprintf(display, "%6.3fkm ", tempMileage);
                    OLED_ShowStr(48, 2, (u8*)display, 2);

                    /* 显示闹钟状态 */
                    if (g_data.alarmEnabled)
                    {
                        sprintf(display, "%02d:%02d", g_data.alarmHour, g_data.alarmMin);
                        OLED_ShowStr(48, 4, (u8*)display, 2);
                    }
                }

                xSemaphoreGive(g_dataMutex);
            }
            xSemaphoreGive(g_oledMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*===========================================================================
 * Task_Key - 按键任务 (优先级2)
 * 周期: 20ms
 * 功能: 扫描按键，发送事件到队列
 *===========================================================================*/
void Task_Key(void *pvParameters)
{
    u8 keyVal;
    u8 keyEvent;

    (void)pvParameters;

    for (;;)
    {
        keyVal = KEY_Scan(1);  /* 支持连按(设置参数时需要连续加减) */

        if (keyVal != 0)
        {
            /* 按键消抖 - 通过20ms周期自动实现 */
            keyEvent = keyVal;  /* 1-5 对应 KEY_EVENT_SET/PAGE等 */

            /* 发送到队列 */
            xQueueSend(g_keyQueue, &keyEvent, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/*===========================================================================
 * 处理按键事件(在Task_Key上下文中执行)
 *===========================================================================*/
static void ProcessKeyEvent(u8 keyEvent)
{
    u8 i;

    if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        switch (keyEvent)
        {
        case KEY_EVENT_SET:  /* 设置键 */
            g_data.setn++;
            if (g_data.page == 1)
            {
                if (g_data.setn > 6)  /* 增加闹钟设置项 */
                {
                    g_data.setn = 0;
                    Store_Data();
                    if (xSemaphoreTake(g_oledMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                    {
                        OLED_CLS();
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16, 2, i + 44, 0);
                        OLED_ShowChar(32, 2, ':', 2, 0);
                        xSemaphoreGive(g_oledMutex);
                    }
                }
            }
            else
            {
                if (g_data.setn > 12)
                {
                    g_data.setn = 0;
                    Store_Data();
                    if (xSemaphoreTake(g_oledMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                    {
                        OLED_CLS();
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16, 4, i + 16, 1);
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16 + 48, 4, i + 18, 1);
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16 + 95, 4, i + 20, 1);
                        OLED_ShowCentigrade(112, 2);
                        xSemaphoreGive(g_oledMutex);
                    }
                }
            }
            break;

        case KEY_EVENT_UP:  /* 加键 */
            if (g_data.page == 1)
            {
                switch (g_data.setn)
                {
                case 1: g_data.setHour++; if (g_data.setHour >= 100) g_data.setHour = 0; break;
                case 2: g_data.setMin++;  if (g_data.setMin >= 60)   g_data.setMin = 0;  break;
                case 3: g_data.setSec++;  if (g_data.setSec >= 60)   g_data.setSec = 0;  break;
                case 4: if (g_data.stepLength < 200) g_data.stepLength++; break;
                case 5: if (g_data.warnMileage < 20000) g_data.warnMileage += 10; break;
                case 6: /* 闹钟时 */
                    g_data.alarmHour++;
                    if (g_data.alarmHour >= 24) g_data.alarmHour = 0;
                    g_data.alarmEnabled = 1;
                    break;
                }
            }
            else
            {
                switch (g_data.setn)
                {
                case 1:  g_data.dateTime.year++; if (g_data.dateTime.year >= 100) g_data.dateTime.year = 0; DS1302_DateSet(&g_data.dateTime); break;
                case 2:  g_data.dateTime.mon++;  if (g_data.dateTime.mon >= 13)   g_data.dateTime.mon = 1;  DS1302_DateSet(&g_data.dateTime); break;
                case 3:  g_data.dateTime.day++;  if (g_data.dateTime.day >= 32)   g_data.dateTime.day = 1;  DS1302_DateSet(&g_data.dateTime); break;
                case 4:  g_data.dateTime.week++; if (g_data.dateTime.week >= 8)   g_data.dateTime.week = 1; DS1302_DateSet(&g_data.dateTime); break;
                case 5:  g_data.dateTime.hour++; if (g_data.dateTime.hour >= 24)  g_data.dateTime.hour = 0; DS1302_DateSet(&g_data.dateTime); break;
                case 6:  g_data.dateTime.min++;  if (g_data.dateTime.min >= 60)   g_data.dateTime.min = 0;  DS1302_DateSet(&g_data.dateTime); break;
                case 7:  g_data.dateTime.sec++;  if (g_data.dateTime.sec >= 60)   g_data.dateTime.sec = 0;  DS1302_DateSet(&g_data.dateTime); break;
                case 8:  if (g_data.hrMax - g_data.hrMin > 1) g_data.hrMin++; break;
                case 9:  if (g_data.hrMax < 300) g_data.hrMax++; break;
                case 10: if (g_data.spo2Min < 200) g_data.spo2Min++; break;
                case 11: if (g_data.tempMax - g_data.tempMin > 1) g_data.tempMin++; break;
                case 12: if (g_data.tempMax < 999) g_data.tempMax++; break;
                }
            }
            break;

        case KEY_EVENT_DOWN:  /* 减键 */
            if (g_data.page == 1)
            {
                switch (g_data.setn)
                {
                case 1: if (g_data.setHour == 0) g_data.setHour = 100; g_data.setHour--; break;
                case 2: if (g_data.setMin == 0)  g_data.setMin = 60;   g_data.setMin--;  break;
                case 3: if (g_data.setSec == 0)  g_data.setSec = 60;   g_data.setSec--;  break;
                case 4: if (g_data.stepLength > 0) g_data.stepLength--; break;
                case 5: if (g_data.warnMileage >= 10) g_data.warnMileage -= 10; break;
                case 6: /* 闹钟分 */
                    g_data.alarmMin++;
                    if (g_data.alarmMin >= 60) g_data.alarmMin = 0;
                    g_data.alarmEnabled = 1;
                    break;
                }
            }
            else
            {
                switch (g_data.setn)
                {
                case 1:  if (g_data.dateTime.year == 0) g_data.dateTime.year = 100; g_data.dateTime.year--; DS1302_DateSet(&g_data.dateTime); break;
                case 2:  if (g_data.dateTime.mon == 1)  g_data.dateTime.mon = 13;  g_data.dateTime.mon--;  DS1302_DateSet(&g_data.dateTime); break;
                case 3:  if (g_data.dateTime.day == 1)  g_data.dateTime.day = 32;  g_data.dateTime.day--;  DS1302_DateSet(&g_data.dateTime); break;
                case 4:  if (g_data.dateTime.week == 1) g_data.dateTime.week = 8;  g_data.dateTime.week--; DS1302_DateSet(&g_data.dateTime); break;
                case 5:  if (g_data.dateTime.hour == 0) g_data.dateTime.hour = 24; g_data.dateTime.hour--; DS1302_DateSet(&g_data.dateTime); break;
                case 6:  if (g_data.dateTime.min == 0)  g_data.dateTime.min = 60;  g_data.dateTime.min--;  DS1302_DateSet(&g_data.dateTime); break;
                case 7:  if (g_data.dateTime.sec == 0)  g_data.dateTime.sec = 60;  g_data.dateTime.sec--;  DS1302_DateSet(&g_data.dateTime); break;
                case 8:  if (g_data.hrMin > 0) g_data.hrMin--; break;
                case 9:  if (g_data.hrMax - g_data.hrMin > 1) g_data.hrMax--; break;
                case 10: if (g_data.spo2Min > 0) g_data.spo2Min--; break;
                case 11: if (g_data.tempMin > 0) g_data.tempMin--; break;
                case 12: if (g_data.tempMax - g_data.tempMin > 1) g_data.tempMax--; break;
                }
            }
            break;

        case KEY_EVENT_CLEAR:  /* 清零键 */
            if (g_data.page == 0)
            {
                g_data.steps = 0;
                Store_Data();
            }
            else
            {
                if (g_data.mileageReminder)
                {
                    g_data.mileageReminder = 0;
                    BEEP = 0;
                }
                else
                {
                    g_data.mileage = 0;
                    g_data.mileageSteps = 0;
                    Store_Data();
                }
            }
            break;

        case KEY_EVENT_PAGE:  /* 页面切换 */
            if (g_data.setn == 0)
            {
                g_data.page = !g_data.page;
                if (xSemaphoreTake(g_oledMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    OLED_CLS();
                    if (g_data.page == 0)
                    {
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16, 4, i + 16, 1);
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16 + 48, 4, i + 18, 1);
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16 + 95, 4, i + 20, 1);
                        OLED_ShowCentigrade(112, 2);
                    }
                    else
                    {
                        for (i = 0; i < 2; i++) OLED_ShowCN(i * 16, 2, i + 44, 0);
                        OLED_ShowChar(32, 2, ':', 2, 0);
                    }
                    xSemaphoreGive(g_oledMutex);
                }
            }
            break;
        }

        xSemaphoreGive(g_dataMutex);
    }
}

/*===========================================================================
 * Task_Alarm - 报警任务 (优先级3)
 * 周期: 100ms
 * 功能: 健康异常报警、定时提醒、里程提醒、闹钟
 *===========================================================================*/
void Task_Alarm(void *pvParameters)
{
    u8 keyEvent;
    u8 beepCount = 0;
    u16 alarmBeepCnt = 0;
    static u16 timeCount1s = 0;

    (void)pvParameters;

    for (;;)
    {
        if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            /* 检查按键事件(任意键关闭闹钟/报警) */
            if (xQueueReceive(g_keyQueue, &keyEvent, 0) == pdTRUE)
            {
                /* 先处理按键事件 */
                ProcessKeyEvent(keyEvent);
            }

            /*=== 报警优先级: 健康异常 > 闹钟 > 定时提醒 > 里程提醒 ===*/

            /* 1. 健康异常报警 */
            if ((g_data.heartRate != 0 && (g_data.heartRate >= g_data.hrMax || g_data.heartRate <= g_data.hrMin)) ||
                (g_data.spo2 != 0 && g_data.spo2 <= g_data.spo2Min) ||
                (g_data.temperature >= g_data.tempMax || g_data.temperature <= g_data.tempMin))
            {
                BEEP = ~BEEP;
                g_data.beepFlag |= ALARM_HEALTH;
            }
            else
            {
                g_data.beepFlag &= ~ALARM_HEALTH;

                /* 2. 闹钟检查 */
                if (g_data.alarmEnabled &&
                    g_data.dateTime.hour == g_data.alarmHour &&
                    g_data.dateTime.min == g_data.alarmMin &&
                    g_data.dateTime.sec < 10)  /* 响10秒 */
                {
                    alarmBeepCnt++;
                    if (alarmBeepCnt % 5 == 0) BEEP = ~BEEP;
                }
                else
                {
                    alarmBeepCnt = 0;

                    /* 3. 定时提醒 */
                    if (g_data.timingReminder && !(g_data.beepFlag & ALARM_TIMING))
                    {
                        BEEP = ~BEEP;
                    }
                    else
                    {
                        /* 4. 里程提醒 */
                        if (g_data.mileage >= g_data.warnMileage && g_data.warnMileage > 0)
                        {
                            g_data.mileageReminder = 1;
                            beepCount++;
                            if (beepCount == 3)  BEEP = 1;
                            if (beepCount == 6)  BEEP = 0;
                            if (beepCount == 9)  BEEP = 1;
                            if (beepCount == 12) BEEP = 0;
                            if (beepCount >= 40) { BEEP = 0; beepCount = 0; }
                        }
                        else
                        {
                            g_data.mileageReminder = 0;
                            beepCount = 0;
                            if (!g_data.timingReminder) BEEP = 0;
                        }
                    }
                }
            }

            /* 秒表计时(1秒更新) */
            timeCount1s++;
            if (timeCount1s >= 10)
            {
                timeCount1s = 0;
                if (g_data.timingRunning)
                {
                    if (g_data.timeCount < (98 * 3600 + 59 * 60))
                    {
                        g_data.timeCount++;
                    }
                    if (g_data.timeCount == (g_data.setHour * 3600 + g_data.setMin * 60 + g_data.setSec))
                    {
                        g_data.timingReminder = 1;
                        g_data.beepFlag |= ALARM_TIMING;
                    }
                }
            }

            xSemaphoreGive(g_dataMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*===========================================================================
 * Task_Bluetooth - 蓝牙通信任务 (优先级1, 最低)
 * 功能: 接收蓝牙命令，上报传感器数据
 *===========================================================================*/
void Task_Bluetooth(void *pvParameters)
{
    char *str1, *str2;
    int setValue;
    char setvalue[4];
    u8 i;
    u16 sendTimer = 0;

    (void)pvParameters;

    /* 初始化串口 */
    uart1_Init(9600);

    for (;;)
    {
        /* 等待UART接收信号量(由中断给出) */
        if (xSemaphoreTake(g_uartRxSem, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            /* 处理接收数据 */
            if (strlen((char*)Usart1RecBuf) > 0)
            {
                if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                {
                    if (strstr((char*)Usart1RecBuf, "setHeartMin:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setHeartMin:");
                        while (*str1 < '0' || *str1 > '9') str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '\r') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 300) g_data.hrMin = setValue;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setHeartMax:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setHeartMax:");
                        while (*str1 < '0' || *str1 > '9') str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '\r') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 300) g_data.hrMax = setValue;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setSpo2Min:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setSpo2Min:");
                        while (*str1 != ':') str1++;
                        str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '\r') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 200) g_data.spo2Min = setValue;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setTempMin:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setTempMin:");
                        while (*str1 < '0' || *str1 > '9') str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '.') break; }
                        if (*str1 == '.') str1++;
                        if (*str1 >= '0' && *str1 <= '9') setvalue[i] = *str1;
                        i++;
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 999) g_data.tempMin = setValue;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setTempMax:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setTempMax:");
                        while (*str1 < '0' || *str1 > '9') str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '.') break; }
                        if (*str1 == '.') str1++;
                        if (*str1 >= '0' && *str1 <= '9') setvalue[i] = *str1;
                        i++;
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 999) g_data.tempMax = setValue;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setSteps:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setSteps:");
                        while (*str1 < '0' || *str1 > '9') str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '\r') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 200) g_data.stepLength = setValue;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setMileage:") != NULL)
                    {
                        str1 = strstr((char*)Usart1RecBuf, "setMileage:");
                        while (*str1 < '0' || *str1 > '9') str1++;
                        i = 0;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '.') break; }
                        if (*str1 == '.') str1++;
                        while (*str1 >= '0' && *str1 <= '9') { setvalue[i++] = *str1++; if (*str1 == '\r') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 999) g_data.warnMileage = setValue * 10;
                    }
                    else if (strstr((char*)Usart1RecBuf, "setAlarm:") != NULL)
                    {
                        /* 闹钟设置: setAlarm:HH:MM */
                        str2 = strstr((char*)Usart1RecBuf, "setAlarm:");
                        while (*str2 < '0' || *str2 > '9') str2++;
                        i = 0;
                        while (*str2 >= '0' && *str2 <= '9') { setvalue[i++] = *str2++; if (*str2 == ':') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 23) g_data.alarmHour = setValue;
                        str2++;
                        i = 0;
                        while (*str2 >= '0' && *str2 <= '9') { setvalue[i++] = *str2++; if (*str2 == '\r') break; }
                        setvalue[i] = '\0';
                        setValue = atoi(setvalue);
                        if (setValue >= 0 && setValue <= 59) g_data.alarmMin = setValue;
                        g_data.alarmEnabled = 1;
                    }
                    else if (strstr((char*)Usart1RecBuf, "alarmOff") != NULL)
                    {
                        g_data.alarmEnabled = 0;
                    }
                    else if (strstr((char*)Usart1RecBuf, "warntime") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        str2 = strstr((char*)Usart1RecBuf, "warntime");
                        while (*str2 < '0' || *str2 > '9') str2++;
                        i = 0;
                        while (*str2 >= '0' && *str2 <= '9') { setvalue[i++] = *str2++; if (*str2 == ':') break; }
                        setvalue[i] = '\0';
                        g_data.setHour = atoi(setvalue);
                        str2++;
                        i = 0;
                        while (*str2 >= '0' && *str2 <= '9') { setvalue[i++] = *str2++; if (*str2 == ':') break; }
                        setvalue[i] = '\0';
                        g_data.setMin = atoi(setvalue);
                        str2++;
                        i = 0;
                        while (*str2 >= '0' && *str2 <= '9') { setvalue[i++] = *str2++; if (*str2 == '\r') break; }
                        setvalue[i] = '\0';
                        g_data.setSec = atoi(setvalue);
                    }
                    else if (strstr((char*)Usart1RecBuf, "stepsClear") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.steps = 0;
                        Store_Data();
                    }
                    else if (strstr((char*)Usart1RecBuf, "mileageClear") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.mileage = 0;
                        g_data.mileageSteps = 0;
                        Store_Data();
                    }
                    else if (strstr((char*)Usart1RecBuf, "timeClear") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.timingReminder = 0;
                        g_data.timingRunning = 0;
                        g_data.timeCount = 0;
                    }
                    else if (strstr((char*)Usart1RecBuf, "startTiming") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.timingRunning = 1;
                    }
                    else if (strstr((char*)Usart1RecBuf, "pauseTiming") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.timingRunning = 0;
                    }
                    else if (strstr((char*)Usart1RecBuf, "offMileage") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.mileageReminder = 0;
                    }
                    else if (strstr((char*)Usart1RecBuf, "offTime") != NULL)
                    {
                        BEEP = 1; DelayMs(80); BEEP = 0;
                        g_data.timingReminder = 0;
                        g_data.beepFlag &= ~ALARM_TIMING;
                    }

                    Store_Data();
                    xSemaphoreGive(g_dataMutex);
                }

                memset(Usart1RecBuf, 0, USART1_RXBUFF_SIZE);
                RxCounter = 0;
            }
        }

        /* 定期上报数据(每1秒) */
        sendTimer++;
        if (sendTimer >= 100)
        {
            sendTimer = 0;
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                printf("$Heartrate:%d#,$Spo2:%d#,$Temperature:%4.1f#,",
                       (int)g_data.heartRate, (int)g_data.spo2, (float)g_data.temperature / 10);
                printf("$Steps:%d#,$Mileage:%6.3f#,",
                       g_data.steps, (float)g_data.mileage / 1000);
                printf("$Hour:%02d#,", (int)(g_data.timeCount / 3600));
                printf("$Min:%02d#,",  (int)(g_data.timeCount % 3600 / 60));
                printf("$Sec:%02d#",   (int)(g_data.timeCount % 60));
                printf("send ok.");

                xSemaphoreGive(g_dataMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
