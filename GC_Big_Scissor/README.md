# GC_Big_Scissor

大剪举升机 STM32F407 固件工程。当前产品类型是 `large_scissor`，支持主机/子机角色、旋转开关切换、补油、光电/急停/远程锁定保护，以及 TAS 4G DTU MQTT 上报。

## 主要功能

- 大剪主机/子机上升、下降、锁定、补油控制。
- 旋转开关选择主机/子机，角色切换时立即关闭输出。
- 急停、光电、主机下限位光电屏蔽、远程锁定、非法状态保护。
- W25Qxx 保存运行统计和操作日志。
- DTU 通过 MQTT 接入 Web 平台，遥测/事件上报约 1 秒 1 包。

## 目录说明

| 目录 | 内容 |
|---|---|
| `APP/Inc`, `APP/Src` | 产品状态机、IoT JSON、DTU 协议、W25Qxx 存储、操作日志、IO 映射 |
| `Driver/Inc`, `Driver/Src` | FreeRTOS 任务封装和驱动门面，例如 `dri_lift.c`、`dri_tas_dtu.c` |
| `Core/Inc`, `Core/Src` | CubeMX 生成的主入口、FreeRTOS、GPIO、UART、SPI、DMA |
| `BSP` | SPI、UART、W25Qxx 等板级驱动 |
| `RTT_easylog` | SEGGER RTT + EasyLogger 源码 |
| `Drivers`, `Middlewares` | STM32 HAL、CMSIS、FreeRTOS 等第三方库 |
| `docs` | 历史设计说明和调试计划 |
| `MDK-ARM`, `EWARM`, `cmake` | Keil/IAR/CMake 工程文件 |

## 关键文件

- `APP/Src/lift_large_scissor.c`：大剪动作状态机。
- `APP/Src/lift_core.c`：公共安全层和产品 ops 调度。
- `APP/Src/app_io_map.c`：输入输出映射、去抖和 IOMAP 日志。
- `APP/Src/lift_iot.c`：遥测 JSON、远程锁机/清报警/配置命令处理。
- `APP/Src/app_tas_dtu.c`：TAS DTU AT/MQTT 协议。
- `Driver/Src/dri_tas_dtu.c`：DTU FreeRTOS 任务和上报节流。

## 构建与调试

- Keil 工程在 `MDK-ARM`。
- 常用脚本：`build_keil.bat`、`flash_keil.bat`、`flash_and_run.jlink`。
- RTT/EasyLogger 是源码依赖，临时 `*.log`、`rtt_capture*.txt`、`jlink_*.txt` 不应提交。
