/**
 * @file    delay.c
 * @brief   延时函数 - 适配FreeRTOS
 *          SysTick由FreeRTOS接管，DelayUs使用忙等待实现
 *          DelayMs在调度器运行后使用vTaskDelay
 */

#include "stm32f10x.h"
#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"

/* 72MHz下，每个NOP约1/72us，循环一次约4个周期 => 1us约18次循环 */
static uint32_t fac_us = 0;

/**
 * @brief 初始化延时函数
 */
void DelayInit(void)
{
    fac_us = SystemCoreClock / 1000000;  /* 72MHz => 72 */
}

/**
 * @brief 延时nus微秒 - 忙等待
 * @param nus 微秒数
 */
void DelayUs(unsigned long nus)
{
    uint32_t ticks = nus * fac_us;
    uint32_t told, tnow, tcnt = 0;
    told = SysTick->VAL;
    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
                tcnt += told - tnow;
            else
                tcnt += SysTick->LOAD - tnow + told;
            told = tnow;
            if (tcnt >= ticks) break;
        }
    }
}

/**
 * @brief 延时nms毫秒
 * @param nms 毫秒数
 */
void DelayMs(unsigned int nms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        /* 调度器未启动，忙等待 */
        while (nms--)
        {
            DelayUs(1000);
        }
    }
    else
    {
        /* 调度器已启动，使用FreeRTOS延时(让出CPU) */
        vTaskDelay(nms);
    }
}

/**
 * @brief 延时ns秒
 */
void DelayS(unsigned int ns)
{
    while (ns--)
    {
        DelayMs(1000);
    }
}
