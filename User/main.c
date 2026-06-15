/**
 * @file    main.c
 * @brief   基于FreeRTOS的智能手表 - 主程序
 *          初始化外设 → 创建任务 → 启动调度器
 *
 * 任务列表:
 *   Task_Sensor    (优先级4) - 传感器采集: DS1302/DS18B20/ADXL345/MAX30102
 *   Task_Display   (优先级3) - OLED显示: 时间/心率/血氧/体温/步数/里程
 *   Task_Alarm     (优先级3) - 报警管理: 健康异常/定时提醒/里程提醒/闹钟
 *   Task_Key       (优先级2) - 按键处理: 5个按键扫描与设置
 *   Task_Bluetooth (优先级1) - 蓝牙通信: USART1数据收发
 *
 * 任务间通信:
 *   g_dataMutex  - 互斥锁, 保护共享数据结构g_data
 *   g_oledMutex  - 互斥锁, 保护OLED显示操作
 *   g_uartRxSem  - 二值信号量, UART接收中断通知
 *   g_keyQueue   - 队列, 按键事件传递
 */

#include "stm32f10x.h"
#include "OLED_I2C.h"
#include "delay.h"
#include "key.h"
#include "app_tasks.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief  TIM2中断处理(保留空实现,避免链接错误)
 *         原TIM2的定时逻辑已移至Task_Alarm中通过vTaskDelay实现
 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

/**
 * @brief  FreeRTOS栈溢出钩子(当configCHECK_FOR_STACK_OVERFLOW>=2时必须实现)
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* 栈溢出时进入死循环，便于调试 */
    for (;;) {}
}

/**
 * @brief  主函数 - FreeRTOS入口
 */
int main(void)
{
    /* 1. 系统初始化 */
    DelayInit();                                    /* 延时函数初始化(DWT) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); /* NVIC优先级分组 */

    /* 2. 外设初始化 */
    KEY_Init();                                     /* 按键 + 蜂鸣器 */
    I2C_Configuration();                            /* 硬件I2C1(OLED) */
    OLED_Init();                                    /* OLED显示屏 */

    /* 3. 创建FreeRTOS任务 */
    AppTaskCreate();

    /* 4. 启动FreeRTOS调度器(永不返回) */
    vTaskStartScheduler();

    /* 不应执行到这里 */
    for (;;) {}
}
