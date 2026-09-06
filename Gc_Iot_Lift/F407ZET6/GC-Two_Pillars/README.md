# GC-Two_Pillars

两柱举升机 STM32F407 固件工程。工程保留多产品平台代码，但当前主要使用 `double_post` 两柱模式，包含上升、下降、锁定、电磁铁解锁、上限位保护、急停和远程锁定。

## 主要功能

- 两柱上升、下降、锁定控制。
- 下降时先启动电机，200ms 后吸合电磁铁，累计 2000ms 后关闭电机并打开下降阀。
- 上限位或锁定按钮可触发下降过程中的强制解锁路径。
- TAS 4G DTU MQTT 上报和 Web 远程命令接入。
- IOMAP 启动快照、输入变化、DTU 任务创建和 MQTT 状态日志。

## 目录说明

| 目录 | 内容 |
|---|---|
| `APP/Inc`, `APP/Src` | 两柱状态机、公共 lift core、IO 映射、IoT/DTU、存储和操作日志 |
| `Driver/Inc`, `Driver/Src` | FreeRTOS 任务与驱动封装 |
| `Core/Inc`, `Core/Src` | CubeMX 生成入口、FreeRTOS、GPIO、UART、SPI、DMA |
| `BSP` | 板级 SPI/UART/W25Qxx 支持 |
| `RTT_easylog` | SEGGER RTT + EasyLogger 源码 |
| `Drivers`, `Middlewares` | HAL/CMSIS/FreeRTOS 库 |
| `docs` | 历史方案、MQTT 修复计划和产品平台说明 |
| `MDK-ARM`, `EWARM`, `cmake` | 工程文件和构建配置 |

## 关键文件

- `APP/Src/lift_double_post.c`：两柱动作状态机。
- `APP/Src/lift_core.c`：公共安全层和产品 ops 调度。
- `APP/Src/app_io_map.c`：两柱 IO 映射、IOMAP boot snapshot 和输入变化日志。
- `APP/Src/lift_iot.c`：遥测和远程命令处理。
- `APP/Src/app_tas_dtu.c`：DTU AT/MQTT 协议。
- `Driver/Src/dri_tas_dtu.c`：DTU 任务、重连和 1 秒上报节流。

## 构建与调试

- Keil 工程在 `MDK-ARM`。
- 常用脚本：`build_keil.bat`、`flash_and_run.jlink`。
- `RTT_easylog` 是源码库，不属于临时日志；运行输出日志应保持清理。
