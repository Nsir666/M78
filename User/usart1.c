/**
 * @file    usart1.c
 * @brief   USART1驱动 - 适配FreeRTOS
 *          接收中断中通过信号量通知蓝牙任务
 */

#include <stm32f10x.h>
#include <stdio.h>
#include "usart1.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* 外部信号量句柄(在app_tasks.c中定义) */
extern SemaphoreHandle_t g_uartRxSem;

char Usart1RecBuf[USART1_RXBUFF_SIZE];  /* 接收缓冲区 */
unsigned int RxCounter = 0;              /* 接收计数器 */

/* printf重定向(MicroLIB) */
#if 1
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

FILE __stdout;

_sys_exit(int x)
{
    x = x;
}

int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);
    USART1->DR = (u8)ch;
    return ch;
}
#endif

/**
 * @brief  USART1初始化
 * @param  bound 波特率(通常9600)
 * @note   PA9=TX, PA10=RX
 */
void uart1_Init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    USART_DeInit(USART1);

    /* USART1_TX PA.9 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART1_RX PA.10 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* NVIC配置 - 优先级需低于configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5) */
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;  /* 必须 >= 5 才能使用FreeRTOS API */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* USART配置 */
    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);  /* 接收中断 */
    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);  /* IDLE中断(检测一帧结束) */
    USART_Cmd(USART1, ENABLE);
}

/**
 * @brief  USART1中断处理
 * @note   RXNE: 接收字节存入缓冲区
 *         IDLE: 一帧接收完成，通知蓝牙任务
 */
void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 接收中断 */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        if (RxCounter >= USART1_RXBUFF_SIZE) RxCounter = 0;
        Usart1RecBuf[RxCounter++] = USART_ReceiveData(USART1);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }

    /* IDLE中断 - 一帧数据接收完成 */
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        (void)USART1->SR;  /* 先读SR */
        (void)USART1->DR;  /* 再读DR清除IDLE标志 */

        /* 通知蓝牙任务处理数据 */
        if (g_uartRxSem != NULL)
        {
            xSemaphoreGiveFromISR(g_uartRxSem, &xHigherPriorityTaskWoken);
        }
    }

    /* 溢出标志清除 */
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) == SET)
    {
        USART_ClearFlag(USART1, USART_FLAG_ORE);
    }

    /* 如果需要，触发上下文切换 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  USART1发送字符串
 */
void uart1_SendStr(char *SendBuf)
{
    while (*SendBuf)
    {
        while ((USART1->SR & 0X40) == 0);
        USART1->DR = (u8)*SendBuf;
        SendBuf++;
    }
}

/**
 * @brief  USART1发送数据
 * @param  bufs 数据指针
 * @param  len  数据长度(0xFF表示按字符串发送)
 */
void uart1_send(unsigned char *bufs, unsigned char len)
{
    if (len != 0xFF)
    {
        while (len--)
        {
            while ((USART1->SR & 0X40) == 0);
            USART1->DR = (u8)*bufs;
            bufs++;
        }
    }
    else
    {
        for (; *bufs != 0; bufs++)
        {
            while ((USART1->SR & 0X40) == 0);
            USART1->DR = (u8)*bufs;
        }
    }
}
