/**
  ******************************************************************************
  * @file    stm32f10x_it.c
  * @brief   中断服务函数 - 适配FreeRTOS
  *          SVC_Handler / PendSV_Handler 由FreeRTOS port.c提供
  *          SysTick_Handler 调用FreeRTOS xPortSysTickHandler
  ******************************************************************************
  */

#include "stm32f10x_it.h"
#include "FreeRTOS.h"
#include "task.h"

/* FreeRTOS SysTick handler - 在port.c中定义 */
extern void xPortSysTickHandler(void);

/* USART1接收中断声明 */
extern void USART1_IRQHandler(void);

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

/*
 * SVC_Handler 由FreeRTOS port.c中的vPortSVCHandler实现
 * 通过FreeRTOSConfig.h中的宏映射: #define vPortSVCHandler SVC_Handler
 * 此处不再定义，否则会重复定义
 */

void DebugMon_Handler(void)
{
}

/*
 * PendSV_Handler 由FreeRTOS port.c中的xPortPendSVHandler实现
 * 通过FreeRTOSConfig.h中的宏映射: #define xPortPendSVHandler PendSV_Handler
 * 此处不再定义，否则会重复定义
 */

/**
 * @brief SysTick中断处理 - 调用FreeRTOS内核 tick处理
 */
void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}
