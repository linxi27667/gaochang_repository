# GC_Small_Scissor

小剪举升机 STM32F407 固件工程。核心控制采用集中式状态机，按 10ms 周期执行输入采集、安全检查、状态切换、输出仲裁和最终安全闸门。

## 主要功能

- 小剪上升、下降、锁定、补油控制。
- 远程锁定、急停、光电报警、下限位屏蔽光电、上升/下降冲突保护。
- 上升和补油均为电机先开，200ms 后气阀打开；补油忽略上限位。
- 下降为电机预启动、气阀打开、电机保持 3000ms、再切换下降阀。
- TAS 4G DTU MQTT 上报和 Web 远程命令接入。

## 目录说明

| 目录 | 内容 |
|---|---|
| `APP/Inc`, `APP/Src` | 小剪状态机、IO 映射、IoT/DTU、W25Qxx、操作日志 |
| `Driver/Inc`, `Driver/Src` | lift/DTU/key/motor/safety 等驱动门面和任务封装 |
| `Core/Inc`, `Core/Src` | CubeMX 入口、FreeRTOS、外设初始化 |
| `BSP` | 板级 UART/SPI/W25Qxx |
| `RTT_easylog` | SEGGER RTT + EasyLogger 源码 |
| `Tests` | 主机侧逻辑测试和假运行时 |
| `Drivers`, `Middlewares` | HAL/CMSIS/FreeRTOS 库 |
| `MDK-ARM` | Keil 工程 |

## 关键文件

- `APP/Src/app_lift_core.c`：小剪集中状态机和输出仲裁。
- `APP/Src/app_io_map.c`：输入输出映射、去抖和 IOMAP 日志。
- `APP/Src/app_lift_small_scissor.c`：小剪产品适配。
- `APP/Src/lift_iot.c`：遥测 JSON 和远程命令处理。
- `APP/Src/app_tas_dtu.c`：DTU AT/MQTT 协议。
- `Tests/small_scissor_logic`：小剪逻辑测试。

## 构建与调试

- Keil 工程在 `MDK-ARM`。
- 主机测试脚本在 `Tests/small_scissor_logic/run_all.ps1`。
- 临时 `*.log`、`rtt_capture*.txt`、`rtt_logger*.txt`、`jlink_*.txt` 属于调试输出，可清理。
