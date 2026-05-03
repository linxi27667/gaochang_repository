# STM32F407VET6 丝杆举升机项目 — 架构交接日志

## 项目概述

- MCU: STM32F407VET6 (Cortex-M4)
- RTOS: FreeRTOS (CMSIS-OS v1 wrapper)
- Flash: W25Qxx (JEDEC 0xEF4014 = W25Q80，1MB)
- 日志: EasyLogger (elog)
- 工具链: MDK-ARM (Keil)

## 目录结构

```
f407vet6/
├── APP/                      # 应用层
│   ├── Inc/
│   │   ├── app.h             # 全局配置 (W25Qxx_DEBUG_MODE=1)
│   │   ├── app_spi.h         # SPI总线声明
│   │   └── app_w25qxx.h      # 存储调度层：slot地址、数据结构、API
│   └── Src/
│       ├── app_spi.c         # SPI硬件绑定 (hspi1, GPIOA.4 CS)
│       ├── app_w25qxx.c      # 核心存储：dual-slot Load/Save/CRC校验
│       └── app_motor.c / app_sensor.c / app_comm.c / app_sync.c
├── BSP/                      # 板级支持包
│   ├── Inc/
│   │   ├── bsp_spi.h         # SPI总线抽象结构体 (spi_bus_t)
│   │   └── bsp_w25qxx.h      # W25Q设备对象、指令集API、W25Q_OK=0 / W25Q_ERR=1
│   └── Src/
│       ├── bsp_spi.c         # SPI总线选择/取消选择
│       └── bsp_w25qxx.c      # W25Q底层命令：读/写/擦除/忙等待
├── Driver/                   # 驱动层
│   ├── Inc/
│   │   └── dri_debug.h       # LED调试任务声明
│   └── Src/
│       └── dri_debug.c       # Debug_Task: LED闪烁 + 计数持久化
├── Core/                     # HAL + RTOS 核心
│   ├── Inc/
│   │   ├── main.h            # 包含stm32f4xx_hal.h
│   │   └── spi.h             # CubeMX SPI声明
│   ├── Src/
│   │   ├── main.c            # 入口
│   │   └── ...
│   └── Startup/
├── Drivers/                  # STM32 HAL + FreeRTOS 库
├── MDK-ARM/                  # Keil工程文件
└── README.md
```

## W25Q 三层架构

```
APP (app_w25qxx.c)           ← 存储调度、dual-slot仲裁、CRC
  ↓
BSP (bsp_w25qxx.c)           ← SPI命令集：Read/Erase/Program/WaitBusy
  ↓
BSP (bsp_spi.c)              ← SPI总线抽象 (Select/Deselect)
  ↓
APP (app_spi.c)              ← 硬件绑定 (hspi1 + PA4-CS)
```

## 双扇区轮转持久化

### 存储结构 (12 bytes)

```c
typedef struct {
    uint32_t magic;          // 0x53544F52 ("STOR")
    uint32_t debug_counter;  // 持久化的计数值
    uint32_t crc;            // magic ^ debug_counter
} w25q_storage_t;
```

### 扇区布局

- Slot A: 0x00000000 (Sector 0)
- Slot B: 0x00001000 (Sector 1, 4KB)

### Load 策略 (App_W25Qxx_Storage_Load)

1. 读两个 slot 并验证 magic + CRC
2. 两个都有效 → 选 debug_counter 更大的
3. 只有一个有效 → 用那个
4. 都无效 → 初始化为 counter=0, 写入默认 magic + CRC

### Save 策略 (App_W25Qxx_Storage_Save)

1. 更新 CRC
2. 读两个 slot 状态
3. 选"非活跃"的 slot 写入（counter 更小的那个）
4. Sector Erase → Page Program（确保断电时至少一个完整备份）

## 调试任务 (dri_debug.c)

- Debug_Task: FreeRTOS task
- 上电从 g_w25q_storage.debug_counter 恢复计数值
- LED 每切换一次 (osDelay 约 250ms)，counter++
- 每次 counter 变化调用 App_W25Qxx_Storage_Save() 写回 Flash
- 可通过串口日志观察: "Blink count: X"

---

## 已修复 Bug 记录（2026-05-03）

### Bug 1 & 2: W25Q_OK/W25Q_ERR 布尔反转

文件: `APP/Src/app_w25qxx.c`

W25Q_OK = 0 表示"成功/有效", W25Q_ERR = 1 表示"失败/无效"。但 `storage_read_slot()` 返回值被当作布尔量使用 (`if (a_ok)`)，导致：
- `if (a_ok)` 在 slot 有效时 (a_ok=0) 为 false，进入错误分支
- Load 在"both valid"分支选了 0xFFFFFFFF（擦除态）而非真正的有效数据
- Save 的 `!a_ok` 判断也完全反了

**修复**: 全部改为显式比较 `a_ok == W25Q_OK` / `a_ok != W25Q_OK`

### Bug 3: SPI 接收时 TX 缓冲区越界

文件: `APP/Src/app_spi.c`

`HW_SPI_Transmit_Receive` 中 `tx_data == NULL` 时，用一个单字节栈变量 `dummy` 作为 TX buffer，然后调用 `HAL_SPI_TransmitReceive(handle, &dummy, rx, size=12, timeout)`。HAL 读取了栈上 11 字节垃圾数据，属于未定义行为。

**修复**: tx_data 为 NULL 时改用 `HAL_SPI_Receive()`（内部自动发送 0xFF dummy 时钟）
