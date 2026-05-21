# STM32F407 举升机项目说明

## 当前结构

| 目录 | 作用 |
| --- | --- |
| `APP/` | 电机、按键、编码器、平衡、安全、W25Q 业务逻辑 |
| `Driver/` | FreeRTOS 任务封装：control、safety、key |
| `BSP/` | SPI、W25Q、UART DMA 板级驱动 |
| `Core/` | CubeMX 生成代码和 USER CODE 区 |
| `MDK-ARM/` | Keil 工程 |

PC 模拟测试代码已经删除，不再维护 `simulate/`、`dri_debug.*` 或模拟编码器/模拟碰撞逻辑。

## 当前硬件语义

- 按键：只保留上升 PE0、下降 PE1。stop/确认键业务逻辑已删除。
- 防撞杆：低电平正常，高电平触发；上限杆禁止上升，下限杆禁止下降。
- 蜂鸣器：本轮仅保留空接口 `App_Buzzer_Alarm()` / `App_Buzzer_Off()`，未绑定真实 IO。
- 高度：编码器脉冲按当前方向增减，`HEIGHT_MM(pulse)` 按 `g_config.screw_lead_mm` 换算。

`Stop_Key` 如果仍出现在 `Core/Inc/main.h`、`Core/Src/gpio.c` 或 `.ioc`，是 CubeMX 生成残留。需要删除 PE2 时请在 CubeMX 修改后重新生成。

## 关键控制规则

- 上升和下降互斥；双键同时按下不会启动，运动中会急停。
- 松手停止仍保留，停止后约 200ms 保存一次高度。
- 软件 0 高度和最大高度不再作为强制上下限；未来显示屏接入时再启用软件限高接口。
- 防撞杆告警允许反向离开，告警解除只看引脚是否恢复低电平。

## W25Q 高度存储

高度使用双 slot：

| slot | 地址 | 内容 |
| --- | --- | --- |
| A | `0x00002000` | `w25q_height_t` |
| B | `0x00003000` | `w25q_height_t` |

`w25q_height_t` 包含 magic、version、sequence、左右柱脉冲和 CRC。上电选择 CRC 有效且 sequence 最新的 slot；保存时写入较旧或无效的 slot，并回读校验。

## 日志

固定心跳日志已删除。日志只在状态变化、运动高度变化、Flash 读写、告警触发/解除时打印。防撞杆解除使用 `A/SAFETY` 级别，方便实机调试时快速定位。
