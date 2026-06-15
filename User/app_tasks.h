/**
 * @file    app_tasks.h
 * @brief   FreeRTOS任务定义 - 共享数据结构、队列、信号量、任务函数声明
 */

#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "ds1302.h"

/*===========================================================================
 * 共享数据结构 - 所有传感器数据和系统状态
 *===========================================================================*/
typedef struct
{
    /* 时间 */
    DATE dateTime;              /* DS1302时间 */

    /* 传感器数据 */
    short temperature;          /* 温度 (0.1°C单位) */
    int32_t heartRate;          /* 心率 (BPM) */
    int32_t spo2;               /* 血氧 (%) */

    /* 计步 */
    uint16_t steps;             /* 步数 */
    long mileage;               /* 里程 (米) */
    uint16_t mileageSteps;      /* 里程计步 */
    uint16_t stepLength;        /* 步长 (cm) */

    /* 秒表/计时器 */
    long timeCount;             /* 秒表计时(秒) */
    uint8_t timingRunning;         /* 秒表运行标志 */
    long setHour, setMin, setSec; /* 定时时分秒 */

    /* 闹钟 */
    uint8_t alarmHour;          /* 闹钟时 */
    uint8_t alarmMin;           /* 闹钟分 */
    uint8_t alarmEnabled;       /* 闹钟使能 */

    /* 报警阈值 */
    uint16_t hrMin;             /* 心率下限 */
    uint16_t hrMax;             /* 心率上限 */
    uint16_t spo2Min;           /* 血氧下限 */
    short tempMin;              /* 温度下限 */
    short tempMax;              /* 温度上限 */
    long warnMileage;           /* 里程目标 (米) */

    /* 报警状态 */
    uint8_t beepFlag;           /* 报警标志位 */
    uint8_t timingReminder;        /* 定时提醒标志 */
    uint8_t mileageReminder;       /* 里程提醒标志 */

    /* 页面 */
    uint8_t page;               /* 当前页面 0=健康 1=运动 */
    uint8_t setn;               /* 设置项编号 */

} SensorData_t;

/*===========================================================================
 * 全局共享变量声明
 *===========================================================================*/
extern SensorData_t g_data;

/*===========================================================================
 * FreeRTOS句柄声明
 *===========================================================================*/
extern SemaphoreHandle_t g_dataMutex;       /* 数据互斥锁 */
extern SemaphoreHandle_t g_oledMutex;       /* OLED互斥锁 */
extern SemaphoreHandle_t g_uartRxSem;       /* UART接收信号量 */
extern QueueHandle_t     g_keyQueue;        /* 按键事件队列 */

/*===========================================================================
 * 任务函数声明
 *===========================================================================*/
void AppTaskCreate(void);                   /* 创建所有任务 */

void Task_Sensor(void *pvParameters);       /* 传感器采集任务 */
void Task_Display(void *pvParameters);      /* 显示任务 */
void Task_Key(void *pvParameters);          /* 按键任务 */
void Task_Bluetooth(void *pvParameters);    /* 蓝牙通信任务 */
void Task_Alarm(void *pvParameters);        /* 报警任务 */

/*===========================================================================
 * 按键事件定义
 *===========================================================================*/
#define KEY_EVENT_NONE      0
#define KEY_EVENT_SET       1   /* 设置键 */
#define KEY_EVENT_UP        2   /* 加键 */
#define KEY_EVENT_DOWN      3   /* 减键 */
#define KEY_EVENT_CLEAR     4   /* 清零键 */
#define KEY_EVENT_PAGE      5   /* 页面切换键 */

/*===========================================================================
 * 报警优先级定义
 *===========================================================================*/
#define ALARM_NONE          0x00
#define ALARM_HEALTH        0x01    /* 健康异常(最高) */
#define ALARM_TIMING        0x02    /* 定时提醒 */
#define ALARM_MILEAGE       0x04    /* 里程提醒 */

#endif /* __APP_TASKS_H */
