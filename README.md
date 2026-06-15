# 基于 FreeRTOS 的智能手表嵌入式系统

> 郑州西亚斯学院 · RTOS课程结课项目 · 2026

基于 STM32F103C8T6 + FreeRTOS V10.4.6 的智能手表，5任务多任务架构，集成 OLED 显示、心率血氧、温度、计步、闹钟和蓝牙通信。

---

## 硬件架构

| 外设 | 型号 | 引脚 | 协议 |
|------|------|------|------|
| 主控 | STM32F103C8T6 (Cortex-M3, 72MHz) | — | — |
| 显示 | OLED 0.96" SSD1306 | PB6(SCL), PB7(SDA) | I2C1 400KHz |
| 时钟 | DS1302 | PC13/PC14/PC15 | 3线SPI |
| 温度 | DS18B20 | PA11 | One-Wire |
| 加速度 | ADXL345 | PA6(SCL), PA7(SDA) | 软件I2C |
| 心率血氧 | MAX30102 | PB10(SDA), PB11(SCL) | 软件I2C |
| 按键 | 5键 (SET/UP/DOWN/CLEAR/PAGE) | PB12~15, PA8 | GPIO |
| 蜂鸣器 | 有源蜂鸣器 | PB9 | GPIO |
| 蓝牙 | HC-05 | PA9(TX), PA10(RX) | USART1 9600bps |

---

## FreeRTOS 任务架构

```
FreeRTOS Kernel (V10.4.6, tick=1000Hz, heap=10KB)
    │
    ├── Task_Sensor    Pri=4  栈512B  200ms    传感器数据采集
    ├── Task_Display   Pri=3  栈512B  100ms    OLED双页面刷新
    ├── Task_Alarm     Pri=3  栈256B  100ms    健康报警/闹钟/定时/里程
    ├── Task_Key       Pri=2  栈256B  20ms     按键扫描与消抖
    └── Task_Bluetooth Pri=1  栈256B  事件驱动  蓝牙串口收发

通信机制:
    g_dataMutex — 互斥锁, 保护共享数据 g_data
    g_oledMutex — 互斥锁, 保护 OLED 显示
    g_keyQueue  — 队列(深度8), 按键事件传递
    g_uartRxSem — 二值信号量, UART IDLE中断 → 蓝牙任务
```

---

## 功能列表

| 功能 | 说明 |
|------|------|
| 时间日期显示 | 年/月/日 星期 时:分:秒 实时走时 |
| 心率监测 | MAX30102 PPG算法, BPM显示 |
| 血氧检测 | SpO2算法, 查表法计算 |
| 体温测量 | DS18B20, 精度0.1°C |
| 计步/里程 | ADXL345加速度阈值检测 |
| 双页面 | 页面1健康监测, 页面2运动秒表 |
| 12项参数设置 | 按键可设时间/心率阈值/血氧阈值/温度阈值 |
| 闹钟 | 可设时:分, 到时蜂鸣器间歇鸣叫 |
| 多级报警 | 健康异常>闹钟>定时>里程, 蜂鸣器分级提示 |
| 蓝牙通信 | USART1 9600bps, 定时上报+远程控制 |
| Flash持久化 | 参数保存至内部Flash, 掉电不丢失 |

---

## 目录结构

```
程序源文件-FreeRTOS/
├── User/                     # 用户代码
│   ├── main.c                # 主入口
│   ├── app_tasks.c           # 5个FreeRTOS任务实现
│   ├── app_tasks.h           # 共享数据结构与声明
│   ├── FreeRTOSConfig.h      # FreeRTOS内核配置
│   ├── OLED_I2C.c/h          # OLED显示驱动
│   ├── ds1302.c/h            # 实时时钟驱动
│   ├── ds18b20.c/h           # 温度传感器驱动
│   ├── adxl345.c/h           # 加速度计驱动
│   ├── max30102.c/h          # 心率传感器驱动
│   ├── max30102_read.c/h     # 传感器读取与滤波
│   ├── algorithm.c/h         # 心率血氧算法(Maxim)
│   ├── myiic.c/h             # 软件I2C(MAX30102用)
│   ├── iic.c/h               # 软件I2C(ADXL345用)
│   ├── usart1.c/h            # USART1 蓝牙串口
│   ├── key.c/h               # 5按键+蜂鸣器
│   ├── delay.c/h             # 延时函数(vTaskDelay)
│   ├── led.c/h               # LED驱动
│   ├── timer.c/h             # TIM2定时器
│   ├── stmflash.c/h          # Flash存储读写
│   └── codetab.h             # 中文字模表
├── FreeRTOS/Source/          # FreeRTOS V10.4.6 内核
│   ├── tasks.c               # 任务管理
│   ├── queue.c               # 队列
│   ├── list.c                # 链表
│   ├── timers.c              # 软件定时器
│   ├── event_groups.c        # 事件组
│   ├── include/              # 内核头文件
│   └── portable/RVDS/ARM_CM3/  # Cortex-M3 移植层
│       ├── port.c
│       └── portmacro.h
├── Libraries/                # STM32标准外设库 V3.5.0
│   ├── CMSIS/                # Cortex-M3 核心支持
│   ├── inc/                  # 外设头文件
│   └── src/                  # 外设源文件
├── Project/RVMDK(uV4)/       # Keil MDK 工程
│   └── HelTec.uvprojx        # µVision5 工程文件
├── Doc/                      # 项目文档
│   ├── 项目文档.md            # WBS+甘特图+架构图+注释规范
│   ├── RTOS论文.md            # 结课论文源文件
│   └── RTOS论文.docx          # 结课论文Word版
├── .gitignore
└── README.md
```

---

## 开发环境

| 工具 | 版本 |
|------|------|
| IDE | Keil uVision5 (µVision V5.06) |
| 编译器 | ARM Compiler 5 (V5.06 update 4) |
| RTOS | FreeRTOS V10.4.6 |
| 标准库 | STM32F10x StdPeriph Lib V3.5.0 |
| 仿真 | Proteus 8 Professional |
| 烧录 | ST-Link V2 (SWD) |

---

## 编译

用 Keil uVision5 打开 `Project/RVMDK(uV4)/HelTec.uvprojx`，点击 Build (F7)。

```
Program Size: Code=46856  RO-data=2176  RW-data=2340  ZI-data=16468
Flash: 73% (46.9KB/64KB) | SRAM: 82% (16.5KB/20KB)
```

---

## 论文与文档

- 📄 [项目文档](Doc/项目文档.md) — WBS + 甘特图 + 架构图 + 注释规范
- 📄 [RTOS结课论文](Doc/RTOS论文.docx) — 8000字完整论文
- 📄 [FreeRTOS任务架构图](Doc/FreeRTOS任务架构图.html)
- 📄 [甘特图](Doc/甘特图.html)
- 📄 [WBS分解表](Doc/WBS分解表.html)

---

## 评分对照

| 评分项 | 分值 | 状态 |
|--------|:---:|:---:|
| 任务模块化划分 (≥4任务) | 15 | ✅ 5任务 |
| 任务优先级设置 | 10 | ✅ 4>3>3>2>1 |
| 任务同步与资源保护 | 10 | ✅ 互斥锁×2+队列+信号量 |
| 延时函数标准化 (vTaskDelay) | 5 | ✅ 全部vTaskDelay |
| 显示设备驱动 | 12 | ✅ OLED正常 |
| 实时时钟驱动 | 10 | ✅ DS1302正常 |
| 其他驱动 | 8 | ✅ 全部正常 |
| 主界面动态走时 | 7 | ✅ |
| 切换页面 | 6 | ✅ 双页面 |
| 闹钟功能 | 4 | ✅ 新增 |
| 时间校准功能 | 3 | ✅ |
| 拓展加分 | +3 | ✅ 7项以上功能 |
| WBS+甘特图 | 4 | ✅ |
| FreeRTOS架构图 | 3 | ✅ |
| 源代码注释 | 3 | ✅ |
| **总分** | **100** | **预计95+** |

---

## License

MIT License · Educational Project
