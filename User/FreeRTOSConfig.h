/*
 * FreeRTOS V10.4.6 配置文件 - STM32F103C8T6
 * 针对64KB Flash / 20KB SRAM 优化
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* STM32F103C8T6: 72MHz */
#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   0
#define configUSE_TICKLESS_IDLE                   0
#define configCPU_CLOCK_HZ                        (72000000UL)
#define configTICK_RATE_HZ                        ((TickType_t)1000)
#define configMAX_PRIORITIES                       5
#define configMINIMAL_STACK_SIZE                   ((uint16_t)64)   /* 64*4=256 bytes */
#define configMAX_TASK_NAME_LEN                    8
#define configUSE_16_BIT_TICKS                     0
#define configIDLE_SHOULD_YIELD                    1
#define configUSE_TASK_NOTIFICATIONS               1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES      1
#define configUSE_MUTEXES                          1
#define configUSE_RECURSIVE_MUTEXES                0
#define configUSE_COUNTING_SEMAPHORES              1
#define configQUEUE_REGISTRY_SIZE                  0
#define configUSE_QUEUE_SETS                       0
#define configUSE_TIME_SLICING                     1
#define configUSE_NEWLIB_REENTRANT                 0
#define configENABLE_BACKWARD_COMPATIBILITY        0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS    0

/* 内存分配 - heap_4.c */
#define configSUPPORT_STATIC_ALLOCATION            0
#define configSUPPORT_DYNAMIC_ALLOCATION           1
#define configTOTAL_HEAP_SIZE                      ((size_t)(10 * 1024))  /* 10KB堆 */

/* 钩子函数 */
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                         0
#define configCHECK_FOR_STACK_OVERFLOW             2  /* 开发阶段开启 */
#define configUSE_MALLOC_FAILED_HOOK               0
#define configUSE_DAEMON_TASK_STARTUP_HOOK         0

/* 运行时统计 */
#define configGENERATE_RUN_TIME_STATS              0
#define configUSE_TRACE_FACILITY                   0
#define configUSE_STATS_FORMATTING_FUNCTIONS       0

/* 协程 - 不使用 */
#define configUSE_CO_ROUTINES                       0
#define configMAX_CO_ROUTINE_PRIORITIES             1

/* 软件定时器 */
#define configUSE_TIMERS                            1
#define configTIMER_TASK_PRIORITY                   2
#define configTIMER_QUEUE_LENGTH                    5
#define configTIMER_TASK_STACK_DEPTH                configMINIMAL_STACK_SIZE

/* 中断优先级 - STM32F103使用4位优先级(0-15) */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS 4
#endif

/* 最低中断优先级(数值最大) */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15

/* 系统可管理的最高中断优先级(数值最小) */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

/* 转换为FreeRTOS格式(左移4位) */
#define configKERNEL_INTERRUPT_PRIORITY        (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY   (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* 断言 */
#define configASSERT(x) if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }

/* FreeRTOS API包含 - 只包含需要的 */
#define INCLUDE_vTaskPrioritySet             0
#define INCLUDE_uxTaskPriorityGet            0
#define INCLUDE_vTaskDelete                  0
#define INCLUDE_vTaskCleanUpResources        0
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTimerPendFunctionCall       0
#define INCLUDE_xQueueGetMutexHolder         0
#define INCLUDE_uxTaskGetStackHighWaterMark  1  /* 开发阶段开启 */

/* 映射FreeRTOS中断处理函数名到标准CMSIS名 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
/* SysTick_Handler 由FreeRTOS接管 */

#endif /* FREERTOS_CONFIG_H */
