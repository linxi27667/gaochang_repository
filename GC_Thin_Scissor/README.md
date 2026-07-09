# GC_Thin_Scissor

超薄小剪举升机 STM32F407 固件工程。与小剪相比，超薄小剪的上升/补油只开电机，下降和锁定会根据下限位强制关闭气阀，避免反挺。

## 主要功能

- 超薄小剪上升、下降、锁定、补油控制。
- 上升：电机 ON，气阀和下降阀 OFF，上限位触发停止。
- 补油：上升+补油同时按下，电机 ON，气阀/下降阀 OFF，忽略上限位。
- 下降：电机预启动，延时后气阀 ON，保持后电机 OFF + 下降阀 ON。
- 下限位触发时下降/锁定强制气阀 OFF。
- TAS 4G DTU MQTT 上报和 Web 远程命令接入。

## 目录说明

| 目录 | 内容 |
|---|---|
| `APP/Inc`, `APP/Src` | 超薄小剪状态机、公共 lift core、IO 映射、IoT/DTU、存储和日志 |
| `Driver/Inc`, `Driver/Src` | FreeRTOS 任务封装和驱动门面 |
| `Core/Inc`, `Core/Src` | CubeMX 入口、FreeRTOS、外设初始化 |
| `BSP` | 板级 UART/SPI/W25Qxx |
| `RTT_easylog` | SEGGER RTT + EasyLogger 源码 |
| `Tests` | 主机侧逻辑测试和假运行时 |
| `Drivers`, `Middlewares` | HAL/CMSIS/FreeRTOS 库 |
| `MDK-ARM` | Keil 工程 |

## 关键文件

- `APP/Src/lift_thin_scissor.c`：超薄小剪动作状态机。
- `APP/Src/lift_core.c`：公共安全层和产品 ops 调度。
- `APP/Src/app_io_map.c`：输入输出映射、启动快照和输入变化日志。
- `APP/Src/lift_iot.c`：遥测 JSON 和远程命令处理。
- `APP/Src/app_tas_dtu.c`：DTU AT/MQTT 协议。
- `Tests/thin_scissor_logic`：超薄小剪逻辑测试。

## 构建与调试

- Keil 工程在 `MDK-ARM`。
- 主机测试脚本在 `Tests/thin_scissor_logic/run_all.ps1`。
- 运行日志、RTT 抓取、J-Link 探测文本属于临时输出，不应长期保留。
