# 类 PLC 控制板最小系统验证说明

## 默认验证内容

固件上电后会保持所有 24 V 输出和继电器关闭，并执行以下验证：

1. 读取并打印芯片 UID、系统时钟和 ADC 时钟。
2. 校准 ADC1，周期读取 `ADC_12V`。
3. 每 100 ms 扫描 10 路低有效光耦输入，状态变化时输出日志。
4. 每 500 ms 翻转 PC13 的 `LED_RUN`。
5. 每 5 s 打印心跳、输入位图、电压估算、剩余堆、历史最小堆和任务栈高水位。

## 原理图管脚映射

| 功能 | MCU 管脚 | 电气有效电平 |
|---|---|---|
| LED_RUN | PC13 | 高有效 |
| ADC_12V | PA0 / ADC1_IN0 | 模拟量 |
| IN0~IN6 | PA1~PA7 | 低有效 |
| IN7~IN8 | PC4~PC5 | 低有效 |
| IN9 | PB0 | 低有效 |
| OUT0~OUT3 | PC3、PC2、PC1、PC0 | 高有效 |
| RELAY0~RELAY3 | PB12~PB15 | 高有效 |
| RELAY4~RELAY5 | PC6~PC7 | 高有效 |
| SWDIO / SWCLK | PA13 / PA14 | 调试接口 |
| HSE | PD0 / PD1 | 8 MHz 晶振 |

输入位图的 bit0~bit9 分别对应 IN0~IN9；bit 为 1 表示对应外部输入有效。

## 预期 RTT 日志

```text
A/CLOCK [0] [CLOCK] sys=72000000 hclk=72000000 pclk1=36000000 pclk2=72000000 adc=12000000 rtos_tick=1000
I/SYS   [0] [SYS] uid=... adc_cal=1 outputs=safe_off
A/TASK  [0] [TASK] min_sys task created ...
A/SYS   [0] [SYS] min_sys task started output_test=0
I/SYS   [5000] [SYS] hb loop=... inputs=0x000 adc=... adc12=12.xxxV heap=... min_heap=... stack_hwm=...
```

## 功率输出测试

当前板级验证固件已启用十路同步循环测试：OUT0~OUT3 与 RELAY0~RELAY5 全部开启 10 s，再全部关闭 10 s，持续循环，并在每次切换时输出 RTT 日志。烧录前必须断开电机、电磁阀、接触器和其他可能运动或带危险电压的负载。

## FRAM 测试

- 器件：FM24CL64B，I2C1，PB6/SCL、PB7/SDA，100 kHz，7 位地址 `0x50`。
- 测试区域：`0x1FF0~0x1FFF`，共 16 字节。
- 流程：备份原数据、写入测试图样、回读比较、恢复原数据、再次回读确认。
- 成功日志：`[FRAM] PASS write/read/verify/restore OK`。
- 失败日志会给出测试步骤编号与 HAL I2C 错误码。

## 当前验证边界

本次已通过 CubeMX 生成、Keil 编译和 J-Link 下载。修复 FreeRTOS SysTick 映射后，已通过 RTT 连续捕获 5 s 周期心跳，确认任务调度、输入扫描、ADC 采样和 EasyLogger 正常运行。PC13 的实际发光效果仍需结合 LED 极性、焊接和现场目视确认。
