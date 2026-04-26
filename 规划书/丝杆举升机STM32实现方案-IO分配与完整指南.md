# STM32 丝杆举升机控制系统 — IO分配与完整实现指南

## 一、系统概述

**目标：** 用 STM32F403VGT6 替代原有 PLC，保留原有控制方式并增加 OMCN 级别的安全与交互功能。

**核心控制逻辑（从 PLC 程序翻译）：**

原 PLC 使用 14 路数字量输入（X0-X13）和 8 路数字量输出（Y0-Y7），通过继电器/接触器控制两台三相异步电机。升降过程通过接近开关检测丝杆转动圈数，双侧误差超过 4 圈时快侧停机等待慢侧，实现动态同步。

**电机类型判断：** 从电气原理图可见，系统使用 **三相异步电动机**（380V，3~），通过交流接触器（KM1-KM4）控制正反转，**无编码器、无变频器**，属于纯开关量控制。接近开关（HE1/HE2）安装在立柱顶部，检测丝杆上的触发块，每转产生一个脉冲用于计圈。

---

## 二、PLC 程序翻译 — GPIO 映射详解

### 2.0 电机控制方式确认

**是的，电机就是纯 GPIO 开关量控制。** 具体链路如下：

```
STM32 GPIO (3.3V高/低)
    ↓ 光耦隔离 (PC817，24V侧)
    ↓ 三极管驱动 (S8050)
    ↓ 24V 中间继电器吸合/释放
    ↓ 继电器常开触点闭合
    ↓ 交流接触器线圈得电 (KM1~KM4)
    ↓ 接触器主触点闭合 → 三相380V接入电机
    ↓ 电机正转(上升) / 反转(下降)
```

**没有任何 PWM、编码器、变频器。** 每个电机只需要 **2 个 GPIO**：一个控制上升接触器，一个控制下降接触器。两个电机一共 **4 个 GPIO 输出**。

关键保护靠的是：
- **硬件互锁**：KM1 和 KM2 的常闭辅助触点互相串联在线圈回路中，物理层面防止同时吸合短路
- **软件互锁**：PLC 程序中同一电机的上升/下降输出永远不会同时为 1

---

### 2.1 原 PLC 输入端口完整映射（X0-X13）

> 以下从 PLC 程序注释文件（COMMENT.wcd）中提取，按原 PLC 地址顺序排列

| PLC 地址 | 原 PLC 标签 | 信号类型 | 电气元件 | 功能详解 | GPIO 映射 |
|----------|-----------|---------|---------|---------|----------|
| **X0** | 急停 | 常闭输入 | SB0 | 按下后切断所有电机输出，最高优先级 | **PC0** |
| **X1** | 1#上升驱动(上限位) | 常闭输入 | SQ1 | 立柱1到达上限位时断开，停止上升 | **PC1** |
| **X2** | 2#上升驱动(上限位) | 常闭输入 | SQ2 | 立柱2到达上限位时断开，停止上升 | **PC2** |
| **X3** | 1#下降驱动(下限位) | 常开输入 | SQ3 | 立柱1到达最低位时闭合，停止下降 + **清零计圈** | **PC3** |
| **X4** | 2#下降驱动(下限位) | 常开输入 | SQ4 | 立柱2到达最低位时闭合，停止下降 + **清零计圈** | **PC4** |
| **X5** | 限位并联 / 螺母传感器 | 常开输入 | SQ5+SQ8 | 立柱1和立柱2的**螺母磨损限位并联**接入。螺母过度磨损时机械结构触发此开关 → 停机报警 | **PC5** |
| **X6** | 限位并联 / 防碰杆 | 常闭输入 | SQ7 | **顶部防碰杆限位**（天花板光电）。检测到障碍物时断开 → 全局急停 | **PC6** |
| **X7** | 上升按键互锁 | 常开输入 | SB1 | 上升按钮。PLC程序中此输入同时作为上升互锁检测 | **PC7** |
| **X8** | 上升按键互锁(副) | 常开输入 | SB1 | 上升按键副信号（程序中用于互锁逻辑判断） | **PC8** |
| **X9** | 下降按键互锁 | 常开输入 | SB2 | 下降按键副信号（程序中用于互锁逻辑判断） | **PC9** |
| **X10** | 记数清零 / 下降驱动 | 常开输入 | SB2 | 下降按钮。按下启动下降 | **PC10** |
| **X11** | 转换开关左 / 右 | 选择开关 | QS1 | 模式选择开关：0=自动同步模式 / 1=单边手动模式（原系统用钥匙开关实现单边升降） | **PA15** |
| **X12** | 霍尔报警 / 记数(柱1) | NPN脉冲输入 | HE1 | 立柱1霍尔接近开关。丝杆每转产生1个脉冲，用于**计圈**和**堵转检测** | **PA0** (TIM5_CH1) |
| **X13** | 霍尔报警 / 记数(柱2) | NPN脉冲输入 | HE2 | 立柱2霍尔接近开关。同上，用于柱2计圈 | **PA1** (TIM5_CH2) |

> **关键发现：** X7/X8 都关联上升按钮，X9/X10 都关联下降按钮。PLC 程序中将同一个物理按钮的信号复制到了多个输入点，分别用于互锁判断和动作触发。在 STM32 中这些只需 **各 1 个 GPIO** 读取即可，软件内部做逻辑复用。
>
> **X5/X6 的处理：** 原 PLC 将两个螺母磨损限位 SQ5/SQ8 **并联** 接到 X5，防碰杆 SQ7 接到 X6。这是两个**独立功能**，STM32 建议分开接线以便分别处理。

### 2.2 原 PLC 输出端口完整映射（Y0-Y7）

| PLC 地址 | 原 PLC 标签 | 功能详解 | 驱动对象 | GPIO 映射 |
|----------|-----------|---------|---------|----------|
| **Y0** | 1#上升驱动 | 立柱1电机 **正转**（上升）接触器 KM1 线圈 | 三相电机1 U1/V1/W1 正序 | **PD0** |
| **Y1** | 1#下降驱动 | 立柱1电机 **反转**（下降）接触器 KM2 线圈 | 三相电机1 反序（两相对调） | **PD1** |
| **Y2** | 2#上升驱动 | 立柱2电机 **正转**（上升）接触器 KM3 线圈 | 三相电机2 U1/V1/W1 正序 | **PD2** |
| **Y3** | 2#下降驱动 | 立柱2电机 **反转**（下降）接触器 KM4 线圈 | 三相电机2 反序（两相对调） | **PD3** |
| **Y4** | 工作状态灯 | 系统正常运行指示灯 HL | LED 指示灯 | **PD4** |
| **Y5** | 蜂鸣器 | 报警/预警声音输出 BZR | 有源蜂鸣器 24V | **PD5** |
| **Y6** | 控制(预留) | 程序中标注为"控制"，电气原理图中未接线，**实际未使用** | — | **PD6**(预留) |
| **Y7** | 控制(预留) | 同上，**实际未使用** | — | **PD7**(预留) |

> **Y0/Y1 互锁（柱1）：** 程序中永远不会同时为 1。切换方向时先关断再延时 50ms 后接通反向。
>
> **Y2/Y3 互锁（柱2）：** 同上。

### 2.3 STM32 完整 GPIO 分配总表

| 序号 | GPIO | 方向 | 信号 | 原 PLC | 工作模式 | 上拉/下拉 |
|------|------|------|------|--------|---------|----------|
| 1 | PA0 | 输入 | 柱1 接近开关 HE1 | X12 | **TIM5_CH1 输入捕获** | 下拉 |
| 2 | PA1 | 输入 | 柱2 接近开关 HE2 | X13 | **TIM5_CH2 输入捕获** | 下拉 |
| 3 | PA2 | 输出 | USART2_TX (IoT) | — | AF 复用 | — |
| 4 | PA3 | 输入 | USART2_RX (IoT) | — | AF 复用 | 上拉 |
| 5 | PA9 | 输出 | USART1_TX (HMI) | — | AF 复用 | — |
| 6 | PA10 | 输入 | USART1_RX (HMI) | — | AF 复用 | 上拉 |
| 7 | PA15 | 输入 | 转换开关 QS1 | X11 | GPIO 输入 | 下拉 |
| 8 | PB0 | 输入 | HMI A键 | — | GPIO 输入 | 下拉 |
| 9 | PB1 | 输入 | HMI B键 | — | GPIO 输入 | 下拉 |
| 10 | PB2 | 输入 | 二次下降确认 | — | GPIO 输入 | 下拉 |
| 11 | PC0 | 输入 | **急停 SB0** | X0 | GPIO 输入 | **上拉**(常闭) |
| 12 | PC1 | 输入 | 上限位 SQ1 柱1 | X1 | GPIO 输入 | 上拉(常闭) |
| 13 | PC2 | 输入 | 上限位 SQ2 柱2 | X2 | GPIO 输入 | 上拉(常闭) |
| 14 | PC3 | 输入 | 下限位 SQ3 柱1 | X3 | GPIO 输入 | 下拉(常开) |
| 15 | PC4 | 输入 | 下限位 SQ4 柱2 | X4 | GPIO 输入 | 下拉(常开) |
| 16 | PC5 | 输入 | 螺母磨损 SQ5+SQ8 | X5 | GPIO 输入 | 下拉(常开) |
| 17 | PC6 | 输入 | 防碰杆 SQ7 | X6 | GPIO 输入 | **上拉**(常闭) |
| 18 | PC7 | 输入 | 上升按钮 SB1 | X7 | GPIO 输入 | 下拉(常开) |
| 19 | PC8 | 输入 | 下降按钮 SB2 | X10 | GPIO 输入 | 下拉(常开) |
| 20 | PD0 | 输出 | **KM1 柱1上升** | Y0 | GPIO 推挽输出 | — |
| 21 | PD1 | 输出 | **KM2 柱1下降** | Y1 | GPIO 推挽输出 | — |
| 22 | PD2 | 输出 | **KM3 柱2上升** | Y2 | GPIO 推挽输出 | — |
| 23 | PD3 | 输出 | **KM4 柱2下降** | Y3 | GPIO 推挽输出 | — |
| 24 | PD4 | 输出 | 指示灯 HL | Y4 | GPIO 推挽输出 | — |
| 25 | PD5 | 输出 | 蜂鸣器 BZR | Y5 | GPIO 推挽/PWM | — |
| 26 | PD6 | 输出 | 预留(原Y6) | Y6 | GPIO 推挽输出 | — |
| 27 | PD7 | 输出 | 预留(原Y7) | Y7 | GPIO 推挽输出 | — |

### 2.4 GPIO 电平逻辑速查表

| 信号 | 常态(无动作) | 动作时 | STM32读取到 | 说明 |
|------|------------|--------|------------|------|
| 急停 SB0 | 闭合 | 按下断开 | 常态=1, 动作=0 | 常闭，上拉输入 |
| 上限位 SQ1/SQ2 | 闭合 | 触碰断开 | 常态=1, 动作=0 | 常闭，上拉输入 |
| 下限位 SQ3/SQ4 | 断开 | 触碰闭合 | 常态=0, 动作=1 | 常开，下拉输入 |
| 防碰杆 SQ7 | 闭合 | 触碰断开 | 常态=1, 动作=0 | 常闭，上拉输入 |
| 螺母磨损 SQ5/SQ8 | 断开 | 触发闭合 | 常态=0, 动作=1 | 常开，下拉输入 |
| 上升按钮 SB1 | 断开 | 按下闭合 | 常态=0, 动作=1 | 常开，下拉输入 |
| 下降按钮 SB2 | 断开 | 按下闭合 | 常态=0, 动作=1 | 常开，下拉输入 |
| 接近开关 HE1/HE2 | 低电平 | 每转1次高脉冲 | 上升沿 = 1圈 | NPN型，下拉输入 |
| KM1~KM4 输出 | 0 (断开) | 1 (吸合) | — | 推挽输出 |

---

## 三、PLC 程序逻辑逐行翻译为 GPIO 行为

### 3.0 原 PLC 程序完整逻辑解析

从 PLC 注释文件中提取出的核心逻辑标签按执行顺序如下：

```
┌─ 输入层 ──────────────────────────────────────────────┐
│ X0  急停         X7  上升按键互锁    X12 霍尔(柱1)     │
│ X1  上限位(柱1)   X8  上升按键互锁(副) X13 霍尔(柱2)   │
│ X2  上限位(柱2)   X9  下降按键互锁    X11 转换开关     │
│ X3  下限位(柱1)   X10 下降驱动(按钮)                   │
│ X4  下限位(柱2)                                        │
│ X5  螺母传感器(SQ5+SQ8并联)                            │
│ X6  防碰杆(SQ7)                                        │
└────────────────────────────────────────────────────────┘

┌─ 处理层 ──────────────────────────────────────────────┐
│ 记数清零 ← 下限位触发时清零计数器                       │
│ 限位并联 ← 两柱螺母磨损信号并联或判断                    │
│ 霍尔报警 ← 电机运行但霍尔无脉冲 → 堵转报警              │
│ 上升按键互锁 ← 上升/下降不能同时按                     │
│ 下降按键互锁 ← 同上                                     │
│ 双稳 ← 运行状态双稳态锁存(保持运行直到条件变化)          │
│ 超转 ← 双柱误差超过4圈判定                              │
│ 停延时 ← 同步停机后的延时恢复                           │
│ 停降延时 ← 下降同步停机延时                             │
│ 下限状态报警 ← 下限位状态参与报警判断                   │
│ 减计数 ← 下降时计数器递减                               │
│ 屏手动 ← 屏幕手动操作模式                               │
│ 超转解除 ← 误差恢复≤4圈后解除同步锁定                   │
└────────────────────────────────────────────────────────┘

┌─ 输出层 ──────────────────────────────────────────────┐
│ Y0 上升正转供电(柱1) ← X7按下 且 无上限位 且 无报警    │
│ Y1 下降反转(柱1)   ← X10按下 且 无下限位 且 无报警     │
│ Y2 上升正转供电(柱2) ← X7按下 且 无上限位 且 无报警    │
│ Y3 下降反转(柱2)   ← X10按下 且 无下限位 且 无报警     │
│ Y4 工作状态灯       ← 系统正常运行时亮                  │
│ Y5 蜂鸣器           ← 预警/报警时鸣响                   │
│ (Y6/Y7 未使用)                                       │
└────────────────────────────────────────────────────────┘
```

### 3.1 每个 GPIO 做什么（PLC 行为 → STM32 行为）

#### 输出 GPIO（控制电机的 4 个核心引脚）

| GPIO | PLC 标签 | 何时输出 HIGH(1) | 何时输出 LOW(0) | 实际效果 |
|------|---------|-----------------|----------------|---------|
| **PD0 (Y0)** | 柱1上升 | 按下上升按钮 **且**：急停未触发 **且** 上限位未到 **且** 无报警 **且** 柱1未被同步锁定 | 上述任一条件满足时变为0 | KM1 吸合 → 柱1电机正转 → 立柱上升 |
| **PD1 (Y1)** | 柱1下降 | 按下下降按钮 **且**：急停未触发 **且** 下限位未到 **且** 无报警 **且** 柱1未被同步锁定 | 上述任一条件满足时变为0 | KM2 吸合 → 柱1电机反转 → 立柱下降 |
| **PD2 (Y2)** | 柱2上升 | 同 PD0，但判断柱2的上限位和同步锁定 | 同 PD0 | KM3 吸合 → 柱2电机正转 → 立柱上升 |
| **PD3 (Y3)** | 柱2下降 | 同 PD1，但判断柱2的下限位和同步锁定 | 同 PD1 | KM4 吸合 → 柱2电机反转 → 立柱下降 |

> **核心规则：** Y0/Y1 永远不同时为 1，Y2/Y3 永远不同时为 1。切换方向时必须先关断再延时。

#### 辅助输出 GPIO

| GPIO | PLC 标签 | 何时 HIGH | 何时 LOW | 实际效果 |
|------|---------|----------|---------|---------|
| **PD4 (Y4)** | 工作状态灯 | 系统正常运行（非报警、非急停） | 报警/急停/空闲 | 绿色指示灯亮表示设备正常 |
| **PD5 (Y5)** | 蜂鸣器 | 见下方蜂鸣器逻辑 | 无预警无报警 | 声音报警 |

#### 蜂鸣器行为表

| 场景 | PD5 行为 | 持续时间 |
|------|---------|---------|
| 举升前预警 | HIGH | 2 秒后自动 LOW |
| 距地面150mm下降阶段 | HIGH（持续长鸣） | 直到底位 |
| 报警状态 | HIGH 0.5s → LOW 0.5s 循环 | 直到报警解除 |
| 正常运行 | LOW | — |

#### 输入 GPIO 行为

| GPIO | PLC 标签 | STM32 读取逻辑 | 备注 |
|------|---------|---------------|------|
| **PC0 (X0)** | 急停 | `if (HAL_GPIO_ReadPin(PC0)==0) 触发急停` | **最高优先级**，任何状态立即停机 |
| **PC1 (X1)** | 上限位柱1 | `if (HAL_GPIO_ReadPin(PC1)==0) 柱1停止上升` | 常闭，正常=1 |
| **PC2 (X2)** | 上限位柱2 | `if (HAL_GPIO_ReadPin(PC2)==0) 柱2停止上升` | 常闭，正常=1 |
| **PC3 (X3)** | 下限位柱1 | `if (HAL_GPIO_ReadPin(PC3)==1) { 柱1停止下降; 柱1计圈清零; }` | 常开，正常=0 |
| **PC4 (X4)** | 下限位柱2 | `if (HAL_GPIO_ReadPin(PC4)==1) { 柱2停止下降; 柱2计圈清零; }` | 常开，正常=0 |
| **PC5 (X5)** | 螺母磨损 | `if (HAL_GPIO_ReadPin(PC5)==1) 触发报警` | 常开，正常=0 |
| **PC6 (X6)** | 防碰杆 | `if (HAL_GPIO_ReadPin(PC6)==0) 全局急停` | 常闭，正常=1 |
| **PC7 (X7)** | 上升按钮 | `if (HAL_GPIO_ReadPin(PC7)==1) 请求上升` | 常开，按下=1 |
| **PC8 (X8)** | 下降按钮 | `if (HAL_GPIO_ReadPin(PC8)==1) 请求下降` | 常开，按下=1 |
| **PA0 (X12)** | 霍尔柱1 | TIM5 输入捕获，每个上升沿 → `col1_pulses++` | NPN脉冲 |
| **PA1 (X13)** | 霍尔柱2 | TIM5 输入捕获，每个上升沿 → `col2_pulses++` | NPN脉冲 |
| **PA15 (X11)** | 转换开关 | `if (HAL_GPIO_ReadPin(PA15)==1) 进入单边模式` | 0=自动 1=单边 |

## 四、核心控制逻辑实现

### 4.1 状态机设计

```
[IDLE 空闲] 
    ├─ 按下上升按钮 → [RISE_LOCK 上升互锁检查] → [RISE_PRE 上升预备(蜂鸣2s)] → [RISING 上升中]
    ├─ 按下下降按钮 → [FALLING 下降中]
    └─ 急停触发 → [EMERGENCY_STOP 急停]

[RISING 上升中]
    ├─ 到达上限位 → 该柱停止上升
    ├─ 防碰杆触发 → 全局急停
    ├─ 误差>4圈 → [SYNC_WAIT 同步等待(快柱停)] → 误差≤4圈 → 回到 RISING
    ├─ 堵转检测 → [ALARM 报警]
    ├─ 松开上升按钮 → [IDLE 空闲]
    └─ 急停触发 → [EMERGENCY_STOP]

[FALLING 下降中]
    ├─ 到达下限位 → 该柱停止下降 + 清零计圈
    ├─ 高度≈150mm → 停机 → 等待二次确认 → 按下确认键 → 继续下降
    ├─ 误差>4圈 → [SYNC_WAIT] → 回到 FALLING
    ├─ 堵转检测 → [ALARM]
    └─ 松开下降按钮 → [IDLE]

[ALARM 报警]
    ├─ 按A键 → 显示报警信息
    ├─ 长按A+B 10秒 → [EMERGENCY_MODE 紧急模式]
    └─ 下限位触发 → 自动解除报警 → [IDLE]

[EMERGENCY_MODE 紧急模式]
    ├─ A+下降 → 立柱1单独下降
    ├─ B+下降 → 立柱2单独下降
    ├─ A+上升 → 立柱1单独上升
    ├─ B+上升 → 立柱2单独上升
    ├─ A+B+下降 → 双柱同步下降
    ├─ A+B+上升 → 双柱同步上升
    └─ 下限位触发 → 自动解除 → [IDLE]
```

### 4.2 双柱同步控制（4圈误差限制）

**原 PLC 逻辑：**
```
IF (柱1计圈 - 柱2计圈) > 4 THEN
    柱1电机停止  // 柱1快，停下来等柱2
ELSE IF (柱2计圈 - 柱1计圈) > 4 THEN
    柱2电机停止  // 柱2快，停下来等柱1
ELSE
    两柱都正常运行
END IF

// 当误差回到≤4圈时，之前停止的柱子自动恢复运行
```

**STM32 实现：**

```c
typedef struct {
    int32_t total_pulses;      // 累计脉冲数（从下限位清零后开始）
    int32_t target_pulses;     // 目标脉冲数（同步用）
    uint8_t  motor_running;    // 电机是否运行
    uint8_t  direction;        // 0=停止, 1=上升, 2=下降
    uint8_t  blocked;          // 是否被同步锁定停止
} Column_t;

Column_t col1 = {0}, col2 = {0};
#define SYNC_THRESHOLD  4  // 4圈误差阈值

void sync_control(Column_t *fast, Column_t *slow) {
    int32_t diff = fast->total_pulses - slow->total_pulses;
    
    if (diff > SYNC_THRESHOLD) {
        fast->blocked = 1;   // 快柱停止
        fast->motor_running = 0;
        slow->blocked = 0;   // 慢柱继续
    } else if (diff <= 0) {
        // 误差消除，快柱恢复
        fast->blocked = 0;
    }
    // diff 在 1~4 范围内，两柱都运行
}

void task_sync_check(void) {
    // 每 50ms 执行一次
    int32_t diff = col1.total_pulses - col2.total_pulses;
    
    if (diff > SYNC_THRESHOLD) {
        // 柱1快于柱2超过4圈
        col1.blocked = 1;
        col2.blocked = 0;
    } else if (diff < -SYNC_THRESHOLD) {
        // 柱2快于柱1超过4圈
        col2.blocked = 1;
        col1.blocked = 0;
    } else {
        // 误差在允许范围内，解除同步锁定
        col1.blocked = 0;
        col2.blocked = 0;
    }
}
```

### 4.3 堵转保护（障碍物检测）

**原 PLC 逻辑：** 电机运行但 2 秒内检测不到接近开关脉冲 → 停机报警

**优化方案：** 缩短到 **0.5 秒**（需实地测试验证）

```c
typedef struct {
    uint32_t last_pulse_tick;  // 上次脉冲的系统tick
    uint32_t motor_start_tick; // 电机启动时刻的tick
    uint8_t  pulse_received;   // 本轮是否收到脉冲
} MotorMonitor_t;

MotorMonitor_t motor1 = {0}, motor2 = {0};

// 在接近开关中断中调用
void on_proximity_pulse(uint8_t column_id) {
    if (column_id == 1) {
        motor1.last_pulse_tick = HAL_GetTick();
        motor1.pulse_received = 1;
        col1.total_pulses++;
    } else {
        motor2.last_pulse_tick = HAL_GetTick();
        motor2.pulse_received = 1;
        col2.total_pulses++;
    }
}

#define STALL_TIMEOUT_MS  500  // 0.5秒堵转判定（原2秒，建议缩短）

void task_stall_check(void) {
    // 每 100ms 执行
    uint32_t now = HAL_GetTick();
    
    // 柱1堵转检测
    if (col1.motor_running && !col1.blocked) {
        if (now - motor1.last_pulse_tick > STALL_TIMEOUT_MS) {
            enter_alarm(ALARM_STALL_COL1);
        }
    }
    
    // 柱2堵转检测
    if (col2.motor_running && !col2.blocked) {
        if (now - motor2.last_pulse_tick > STALL_TIMEOUT_MS) {
            enter_alarm(ALARM_STALL_COL2);
        }
    }
}
```

### 4.4 下限位重置计圈

```c
// 下限位中断/轮询检测
void on_lower_limit(uint8_t column_id) {
    if (column_id == 1) {
        col1.total_pulses = 0;     // 柱1清零
        motor1_col1_direction = DIR_STOP;  // 停止下降
    } else {
        col2.total_pulses = 0;     // 柱2清零
        motor2_col2_direction = DIR_STOP;
    }
    
    // 如果在紧急模式下，触发下限位自动解除紧急模式
    if (system_state == EMERGENCY_MODE) {
        system_state = IDLE;
        alarm_code = ALARM_NONE;
    }
}
```

### 4.5 上限位与防碰杆处理

**现状（PLC）：** 上限位 SQ1/SQ2 和防碰杆 SQ7 **串联** 在一个输入点。

**优化方案：** 改为 **独立输入**，实现不同级别的保护：

| 信号 | 作用 | 恢复方式 |
|------|------|---------|
| SQ1/SQ2 上限位 | 单柱停止上升 | 按下降按钮可恢复 |
| SQ7 防碰杆 | 全局急停，所有电机停止 | 排除障碍后手动复位 |

```c
void on_upper_limit(uint8_t column_id) {
    // 上限位：仅停止该柱上升，不影响下降
    if (column_id == 1 && col1.direction == DIR_RISE) {
        col1.motor_running = 0;
    }
    if (column_id == 2 && col2.direction == DIR_RISE) {
        col2.motor_running = 0;
    }
}

void on_anti_collision(void) {
    // 防碰杆：全局最高级别急停
    enter_alarm(ALARM_ANTI_COLLISION);
    col1.motor_running = 0;
    col2.motor_running = 0;
}
```

### 4.6 声光预警机制

```c
typedef enum {
    BUZZER_OFF,
    BUZZER_PRE_RISE,    // 举升前2秒预警
    BUZZER_LOW_DESCENT, // 150mm下降阶段持续长鸣
    BUZZER_ALARM,       // 报警声（间断）
} BuzzerMode_t;

#define HEIGHT_150MM_PULSES  xx  // 根据丝杆螺距计算
// 假设丝杆导程=10mm/转，150mm=15转，即15个脉冲

void buzzer_control(BuzzerMode_t mode) {
    switch(mode) {
        case BUZZER_PRE_RISE:
            BUZZER_ON();
            delay_ms(2000);  // 鸣响2秒
            BUZZER_OFF();
            break;
        case BUZZER_LOW_DESCENT:
            BUZZER_ON();  // 持续长鸣
            break;
        case BUZZER_ALARM:
            // 间断报警：响0.5秒，停0.5秒，循环
            BUZZER_ON();
            delay_ms(500);
            BUZZER_OFF();
            delay_ms(500);
            break;
        default:
            BUZZER_OFF();
            break;
    }
}

// 150mm检测
void check_150mm_descent(void) {
    // 通过计圈数判断高度
    // 总行程脉冲数在初始化时标定
    int32_t remaining = col1.total_pulses;  // 假设已标定
    
    if (col1.direction == DIR_FALL && remaining <= HEIGHT_150MM_PULSES) {
        // 进入最后下降阶段
        buzzer_control(BUZZER_LOW_DESCENT);
        col1.motor_running = 0;
        system_state = WAITING_SECONDARY_CONFIRM;
    }
}
```

### 4.7 报警状态与紧急操作模式

```c
typedef enum {
    ALARM_NONE,
    ALARM_STALL_COL1,        // 柱1堵转
    ALARM_STALL_COL2,        // 柱2堵转
    ALARM_SYNC_OVER,         // 同步误差超限
    ALARM_ANTI_COLLISION,    // 防碰杆触发
    ALARM_NUT_WEAR,          // 螺母磨损
} AlarmCode_t;

typedef enum {
    STATE_IDLE,
    STATE_RISING,
    STATE_FALLING,
    STATE_SYNC_WAIT,
    STATE_ALARM,
    STATE_EMERGENCY,
    STATE_WAITING_SECONDARY_CONFIRM,
    STATE_EMERGENCY_STOP,
} SystemState_t;

SystemState_t system_state = STATE_IDLE;
AlarmCode_t alarm_code = ALARM_NONE;

// 进入报警
void enter_alarm(AlarmCode_t code) {
    alarm_code = code;
    system_state = STATE_ALARM;
    col1.motor_running = 0;
    col2.motor_running = 0;
    // 关闭所有接触器输出
    MOTOR1_RISE_OFF(); MOTOR1_FALL_OFF();
    MOTOR2_RISE_OFF(); MOTOR2_FALL_OFF();
    // 蜂鸣器间断报警
    buzzer_mode = BUZZER_ALARM;
    // 发送报警信息到HMI
    hmi_send_alarm(code);
    // 保存状态到Flash
    save_state_to_flash();
}

// 紧急模式按键组合处理
void emergency_mode_key_handler(void) {
    static uint32_t ab_hold_start = 0;
    static uint8_t a_pressed = 0, b_pressed = 0;
    
    // 报警状态下处理
    if (system_state == STATE_ALARM) {
        // A键单独按：显示报警信息
        if (KEY_A_PRESSED_SINGLE()) {
            hmi_show_alarm_detail(alarm_code);
        }
        
        // A+B同时长按10秒
        if (KEY_A_HELD() && KEY_B_HELD()) {
            if (ab_hold_start == 0) ab_hold_start = HAL_GetTick();
            if (HAL_GetTick() - ab_hold_start > 10000) {
                system_state = STATE_EMERGENCY;
                ab_hold_start = 0;
            }
        } else {
            ab_hold_start = 0;
        }
    }
    
    // 紧急模式下的操作
    if (system_state == STATE_EMERGENCY) {
        // B键确认进入紧急操作流程
        if (KEY_B_PRESSED_SINGLE()) {
            // 进入可操作状态
        }
        
        // A+下降 → 柱1单独下降
        if (KEY_A_HELD() && KEY_FALL_PRESSED()) {
            MOTOR1_FALL_ON(); MOTOR1_RISE_OFF();
            MOTOR2_RISE_OFF(); MOTOR2_FALL_OFF();
        }
        // B+下降 → 柱2单独下降
        if (KEY_B_HELD() && KEY_FALL_PRESSED()) {
            MOTOR2_FALL_ON(); MOTOR2_RISE_OFF();
            MOTOR1_RISE_OFF(); MOTOR1_FALL_OFF();
        }
        // A+上升 → 柱1单独上升
        if (KEY_A_HELD() && KEY_RISE_PRESSED()) {
            MOTOR1_RISE_ON(); MOTOR1_FALL_OFF();
            MOTOR2_RISE_OFF(); MOTOR2_FALL_OFF();
        }
        // B+上升 → 柱2单独上升
        if (KEY_B_HELD() && KEY_RISE_PRESSED()) {
            MOTOR2_RISE_ON(); MOTOR2_RISE_OFF();
            MOTOR1_RISE_OFF(); MOTOR1_FALL_OFF();
        }
        // A+B+下降 → 双柱同步下降
        if (KEY_A_HELD() && KEY_B_HELD() && KEY_FALL_PRESSED()) {
            MOTOR1_FALL_ON(); MOTOR2_FALL_ON();
            MOTOR1_RISE_OFF(); MOTOR2_RISE_OFF();
        }
        // A+B+上升 → 双柱同步上升
        if (KEY_A_HELD() && KEY_B_HELD() && KEY_RISE_PRESSED()) {
            MOTOR1_RISE_ON(); MOTOR2_RISE_ON();
            MOTOR1_FALL_OFF(); MOTOR2_FALL_OFF();
        }
        
        // 松开所有按键时停止
        if (!KEY_ANY_MOTION_PRESSED()) {
            MOTOR1_RISE_OFF(); MOTOR1_FALL_OFF();
            MOTOR2_RISE_OFF(); MOTOR2_FALL_OFF();
        }
    }
}
```

### 4.8 150mm 脚部防压保护（二次确认）

```c
// 高度换算（基于计圈）
// 假设：丝杆导程 = 10mm/转（需实测确认）
// 满行程高度 = H_max mm，对应 N_max 转 = N_max 个脉冲
// 150mm 对应脉冲数 = 150 / 导程

#define SCREW_LEAD_MM        10      // 丝杆导程(mm/转)，需实测
#define HEIGHT_150MM_PULSES  15      // 150mm / 10mm = 15转

// 首次上电需要标定满行程（从下限位到上限位的脉冲数）
int32_t full_travel_pulses = 0;

void calibrate_full_travel(void) {
    // 在下限位时 total_pulses = 0
    // 上升到上限位时的 total_pulses 即为满行程脉冲数
    // 建议出厂标定后固化到Flash
}

// 下降过程150mm检测
void check_secondary_descent(void) {
    if (col1.direction == DIR_FALL) {
        if (col1.total_pulses <= HEIGHT_150MM_PULSES && 
            !col1.secondary_confirmed) {
            // 到达150mm位置，停止
            col1.motor_running = 0;
            col1.direction = DIR_STOP;
            system_state = STATE_WAITING_SECONDARY_CONFIRM;
            
            // HMI提示
            hmi_show_message("FOOT PROTECTION STOP\nPress DOWN again to continue");
        }
    }
}

// 二次确认处理
void handle_secondary_confirm(void) {
    if (system_state == STATE_WAITING_SECONDARY_CONFIRM) {
        if (KEY_FALL_PRESSED()) {
            col1.secondary_confirmed = 1;
            col1.motor_running = 1;
            col1.direction = DIR_FALL;
            system_state = STATE_FALLING;
        }
    }
    
    // 上升时清除二次确认状态
    if (col1.direction == DIR_RISE) {
        col1.secondary_confirmed = 0;
    }
}
```

---

## 五、接触器/电机驱动电路设计

### 5.1 电机控制原理

原系统使用 **三相异步电机**（380V/3~），通过 **交流接触器** 切换相序实现正反转：

```
KM1闭合 → 电机正转 → 立柱上升
KM2闭合 → 电机反转 → 立柱下降
KM1+KM2同时闭合 → 短路！必须互锁！
```

### 5.2 STM32 输出驱动电路

```
STM32 GPIO (3.3V) 
    → 光耦隔离 (PC817) 
    → NPN三极管 (S8050) 
    → 24V继电器线圈 
    → 继电器触点 → 交流接触器线圈 (220V/24V)
```

**硬件互锁（必须）：**
```
KM1线圈回路中串联KM2的常闭辅助触点
KM2线圈回路中串联KM1的常闭辅助触点
→ 物理层面防止同时吸合
```

**软件互锁：**
```c
// 任何情况下，同一柱的上升和下降不能同时输出
void motor1_set_direction(uint8_t dir) {
    if (dir == DIR_RISE) {
        HAL_GPIO_WritePin(M1_FALL_GPIO, M1_FALL_PIN, GPIO_PIN_RESET);
        delay_ms(50);  // 切换死区时间
        HAL_GPIO_WritePin(M1_RISE_GPIO, M1_RISE_PIN, GPIO_PIN_SET);
    } else if (dir == DIR_FALL) {
        HAL_GPIO_WritePin(M1_RISE_GPIO, M1_RISE_PIN, GPIO_PIN_RESET);
        delay_ms(50);
        HAL_GPIO_WritePin(M1_FALL_GPIO, M1_FALL_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(M1_RISE_GPIO, M1_RISE_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(M1_FALL_GPIO, M1_FALL_PIN, GPIO_PIN_RESET);
    }
}
```

---

## 六、数据存储（掉电保护）

### 6.1 必须保存的数据

| 数据项 | 原因 | 写入时机 |
|--------|------|---------|
| 柱1/柱2计圈数 | 停电后丢失会导致严重安全事故 | 每次计圈变化 + 定期保存（每1秒） |
| 系统状态 | 恢复上次运行状态 | 状态变化时 |
| 报警代码 | 停电后需保留报警信息 | 报警发生时 |
| 满行程标定值 | 仅出厂标定一次 | 标定完成后 |
| 用户设置（最大高度等） | 用户配置不丢失 | 修改时 |

### 6.2 STM32 内部 Flash 模拟 EEPROM

```c
// STM32F403VGT6 内部 Flash：1MB
// 使用最后 2 个扇区（Sector 10/11，各128KB）做双页EEPROM模拟

#define FLASH_EEPROM_START  0x080E0000  // Sector 10 起始
#define FLASH_PAGE_SIZE     (128*1024)   // 128KB

typedef struct {
    uint32_t magic;           // 校验魔数 0x4C494654 "LIFT"
    int32_t  col1_pulses;
    int32_t  col2_pulses;
    uint8_t  alarm_code;
    uint8_t  system_state;
    uint32_t full_travel;
    uint32_t max_height;
    uint16_t lift_count;      // 累计举升次数
    uint16_t checksum;
} NVM_Data_t;

void save_to_flash(NVM_Data_t *data) {
    HAL_FLASH_Unlock();
    // 擦除扇区
    FLASH_Erase_Sector(FLASH_SECTOR_10, FLASH_VOLTAGE_RANGE_3);
    // 写入数据
    for (int i = 0; i < sizeof(NVM_Data_t)/4; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, 
            FLASH_EEPROM_START + i*4, 
            ((uint32_t*)data)[i]);
    }
    HAL_FLASH_Lock();
}

void load_from_flash(NVM_Data_t *data) {
    memcpy(data, (void*)FLASH_EEPROM_START, sizeof(NVM_Data_t));
    if (data->magic != 0x4C494654 || verify_checksum(data) != 0) {
        // 数据无效，使用默认值
        memset(data, 0, sizeof(NVM_Data_t));
    }
}
```

> **⚠️ 关键风险点：** 必须在与供应商确认主控板是否具备掉电记忆功能。若停电后计圈数据归零，从下限位到上限位之间的绝对高度信息丢失，会导致同步控制完全失效，可能引发机械碰撞事故。

---

## 七、HMI 显示层设计

### 7.1 推荐方案：方案三（TFT电阻屏 + LVGL）

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| 方案一：原电阻串口屏优化 | 成本低、开发快 | 交互僵硬 | 应急备选 |
| 方案二：电容串口屏 | 界面现代 | 工业油污/手套失效 | **不推荐** |
| 方案三：TFT电阻屏+LVGL | 兼顾工业稳定与高阶UI | 开发量大 | **首选** |

### 7.2 HMI 页面结构

**主界面（首页）：**
- 立柱ID编号
- 当前举升高度（mm 或 脉冲数）
- 双柱最大高度差
- 电池电量（如有ADC监测）
- 联机立柱数量
- 错误代码
- 累计举升次数

**二级界面（需账号密码进入）：**
- 最低位清零
- 最大高度设置
- ID设置
- 起步高度设置
- 联机设置（选择参与联机的立柱）
- 一键调平
- 语言选择

**三级界面：**
- 用户管理（添加/删除用户/修改密码）
- WiFi设置
- 保养信息

### 7.3 通信协议定义（STM32 ↔ HMI）

```
帧格式：[帧头0xAA][长度][命令字][数据N][CRC8][帧尾0x55]

// 命令字定义
#define CMD_HMI_HEARTBEAT       0x01  // 心跳包（1s一次）
#define CMD_HMI_STATUS_UPDATE   0x02  // 状态上报
#define CMD_HMI_ALARM_REPORT    0x03  // 报警上报
#define CMD_HMI_KEY_EVENT       0x10  // 按键事件
#define CMD_HMI_SET_PARAM       0x11  // 参数设置
#define CMD_HMI_GET_PARAM       0x12  // 参数查询

// 状态上报数据结构
typedef struct {
    int32_t col1_pulses;     // 柱1脉冲数
    int32_t col2_pulses;     // 柱2脉冲数
    int32_t pulse_diff;      // 脉冲差值
    uint8_t system_state;    // 系统状态
    uint8_t alarm_code;      // 报警代码
    uint16_t lift_count;     // 举升次数
} HMI_Status_t;
```

---

## 八、物联网（IoT）方案

### 8.1 推荐方案：UART 模块化"子母板"设计

```
STM32 主控板 ←UART→ [插座] ←插拔→ Wi-Fi子板(ESP32) / 4G子板(Air780E)
```

| 特性 | ESP32 Wi-Fi 子板 | 4G Cat.1 子板 |
|------|-----------------|--------------|
| 成本 | ~10元 | ~20元 |
| 依赖 | 需现场Wi-Fi | 独立联网 |
| 配网 | 蓝牙辅助 | 基站自动 |
| 协议 | AT指令MQTT | AT指令MQTT |

### 8.2 动态模块识别

```c
// 主控板预留 4-Pin UART 插座：VCC/GND/TX/RX
// 上电后发送探测指令，根据响应判断模块类型

void detect_iot_module(void) {
    uart_send("AT\r\n", 4);
    if (uart_wait_response("OK", 1000)) {
        uart_send("AT+CGMM\r\n", 9);
        // 根据返回的模块型号判断类型
        if (response_contains("ESP")) {
            iot_module = MODULE_WIFI;
        } else if (response_contains("EC")) {
            iot_module = MODULE_4G;
        }
    } else {
        iot_module = MODULE_NONE;
    }
}
```

### 8.3 MQTT 数据上报

```
主题：device/{device_id}/status
内容：{"col1":1234,"col2":1230,"diff":4,"state":"running","alarm":0,"lifts":5678}

主题：device/{device_id}/alarm
内容：{"code":1,"time":"2026-04-23T10:30:00","desc":"column1_stall"}
```

---

## 九、主程序框架

```c
/* ============================================================
 * 丝杆举升机 STM32F403 主程序框架
 * ============================================================ */

#include "main.h"
#include "gpio.h"
#include "tim.h"
#include "usart.h"
#include "flash_eeprom.h"
#include "motor_control.h"
#include "sensor.h"
#include "sync_logic.h"
#include "alarm.h"
#include "hmi.h"
#include "buzzer.h"

/* 全局变量 */
SystemState_t system_state = STATE_IDLE;
AlarmCode_t alarm_code = ALARM_NONE;
Column_t col1 = {0}, col2 = {0};

/* ============================================================
 * 初始化
 * ============================================================ */
void System_Init(void) {
    HAL_Init();
    SystemClock_Config();  // 240MHz
    GPIO_Init();           // 输入输出引脚
    TIM5_InputCapture_Init();  // 接近开关脉冲捕获
    USART1_Init(115200);   // HMI 通信
    USART2_Init(115200);   // IoT 通信
    
    // 加载Flash中的保存数据
    NVM_Data_t saved;
    load_from_flash(&saved);
    if (saved.magic == 0x4C494654) {
        col1.total_pulses = saved.col1_pulses;
        col2.total_pulses = saved.col2_pulses;
        alarm_code = (AlarmCode_t)saved.alarm_code;
    } else {
        // 首次上电，数据无效
        col1.total_pulses = 0;
        col2.total_pulses = 0;
    }
    
    // 硬件初始化完成，指示灯亮1秒
    HL_ON();
    delay_ms(1000);
    HL_OFF();
    
    hmi_init();
    buzzer_init();
    detect_iot_module();
}

/* ============================================================
 * 主循环（每10ms执行一次完整逻辑）
 * ============================================================ */
int main(void) {
    System_Init();
    
    uint32_t last_save_tick = 0;
    uint32_t last_sync_tick = 0;
    uint32_t last_stall_tick = 0;
    uint32_t last_hmi_tick = 0;
    
    while (1) {
        uint32_t now = HAL_GetTick();
        
        /* 1. 读取输入（限位、按钮状态） */
        read_all_inputs();
        
        /* 2. 急停最高优先级检查 */
        if (is_emergency_triggered()) {
            enter_emergency_stop();
        }
        
        /* 3. 状态机处理 */
        process_state_machine();
        
        /* 4. 同步控制检查（每50ms） */
        if (now - last_sync_tick >= 50) {
            task_sync_check();
            last_sync_tick = now;
        }
        
        /* 5. 堵转检测（每100ms） */
        if (now - last_stall_tick >= 100) {
            task_stall_check();
            last_stall_tick = now;
        }
        
        /* 6. 150mm脚部保护检查 */
        check_secondary_descent();
        
        /* 7. 报警模式按键处理 */
        if (system_state == STATE_ALARM || system_state == STATE_EMERGENCY) {
            emergency_mode_key_handler();
        }
        
        /* 8. 电机输出控制 */
        update_motor_outputs();
        
        /* 9. HMI 通信（每200ms） */
        if (now - last_hmi_tick >= 200) {
            hmi_send_status();
            hmi_process_commands();
            last_hmi_tick = now;
        }
        
        /* 10. 数据保存（每1秒） */
        if (now - last_save_tick >= 1000) {
            NVM_Data_t data;
            data.magic = 0x4C494654;
            data.col1_pulses = col1.total_pulses;
            data.col2_pulses = col2.total_pulses;
            data.alarm_code = alarm_code;
            data.system_state = system_state;
            save_to_flash(&data);
            last_save_tick = now;
        }
        
        /* 11. IoT 数据上报（每5秒，仅当模块存在） */
        if (iot_module != MODULE_NONE && now % 5000 < 10) {
            iot_send_status();
        }
        
        /* 12. 主循环延时 ~10ms */
        delay_ms(10);
    }
}

/* ============================================================
 * 状态机处理函数
 * ============================================================ */
void process_state_machine(void) {
    switch (system_state) {
        case STATE_IDLE:
            if (KEY_RISE_PRESSED()) {
                buzzer_control(BUZZER_PRE_RISE);
                system_state = STATE_RISING;
            } else if (KEY_FALL_PRESSED()) {
                system_state = STATE_FALLING;
            }
            break;
            
        case STATE_RISING:
            if (!KEY_RISE_PRESSED()) {
                system_state = STATE_IDLE;
                stop_all_motors();
            }
            if (is_upper_limit_reached(COL1)) col1.motor_running = 0;
            if (is_upper_limit_reached(COL2)) col2.motor_running = 0;
            break;
            
        case STATE_FALLING:
            if (!KEY_FALL_PRESSED()) {
                system_state = STATE_IDLE;
                stop_all_motors();
            }
            if (is_lower_limit_reached(COL1)) {
                col1.total_pulses = 0;
                col1.motor_running = 0;
            }
            if (is_lower_limit_reached(COL2)) {
                col2.total_pulses = 0;
                col2.motor_running = 0;
            }
            break;
            
        case STATE_SYNC_WAIT:
            // 等待同步恢复
            if (abs(col1.total_pulses - col2.total_pulses) <= SYNC_THRESHOLD) {
                // 同步恢复
                col1.blocked = 0;
                col2.blocked = 0;
                system_state = (col1.direction == DIR_RISE) ? STATE_RISING : STATE_FALLING;
            }
            break;
            
        case STATE_WAITING_SECONDARY_CONFIRM:
            handle_secondary_confirm();
            break;
            
        default:
            break;
    }
}

/* ============================================================
 * 电机输出更新
 * ============================================================ */
void update_motor_outputs(void) {
    // 柱1
    if (col1.motor_running && !col1.blocked) {
        if (col1.direction == DIR_RISE) {
            motor1_set_direction(DIR_RISE);
        } else if (col1.direction == DIR_FALL) {
            motor1_set_direction(DIR_FALL);
        }
    } else {
        motor1_set_direction(DIR_STOP);
    }
    
    // 柱2
    if (col2.motor_running && !col2.blocked) {
        if (col2.direction == DIR_RISE) {
            motor2_set_direction(DIR_RISE);
        } else if (col2.direction == DIR_FALL) {
            motor2_set_direction(DIR_FALL);
        }
    } else {
        motor2_set_direction(DIR_STOP);
    }
}
```

---

## 十、关键定时器配置

### 10.1 TIM5 输入捕获（接近开关脉冲计数）

```c
// TIM5 配置为输入捕获模式
// PA0 → TIM5_CH1 → 立柱1 接近开关
// PA1 → TIM5_CH2 → 立柱2 接近开关

void TIM5_InputCapture_Init(void) {
    TIM_HandleTypeDef htim5;
    TIM_IC_InitTypeDef sConfigIC = {0};
    
    __HAL_RCC_TIM5_CLK_ENABLE();
    
    htim5.Instance = TIM5;
    htim5.Init.Prescaler = 240-1;    // 240MHz/240 = 1MHz (1us分辨率)
    htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim5.Init.Period = 0xFFFFFFFF;   // 32位计数不溢出
    HAL_TIM_Base_Init(&htim5);
    
    // 通道1配置
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 0x0F;  // 最高滤波器（约15us，消除抖动）
    HAL_TIM_IC_ConfigChannel(&htim5, &sConfigIC, TIM_CHANNEL_1);
    
    // 通道2配置
    HAL_TIM_IC_ConfigChannel(&htim5, &sConfigIC, TIM_CHANNEL_2);
    
    HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_2);
}

// 中断回调
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        // 立柱1 脉冲
        col1.total_pulses++;
        motor1.last_pulse_tick = HAL_GetTick();
        motor1.pulse_received = 1;
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        // 立柱2 脉冲
        col2.total_pulses++;
        motor2.last_pulse_tick = HAL_GetTick();
        motor2.pulse_received = 1;
    }
}
```

### 10.2 系统定时基准

```c
// 使用 SysTick 提供 1ms 基准
// 主循环 10ms 周期执行
// 所有延时基于 HAL_GetTick()
```

---

## 十一、三个月实施计划细化

### 11.1 第一个月：环境搭建与底层驱动

| 周次 | 任务 | 交付物 | 验证标准 |
|------|------|--------|---------|
| 第1周 | STM32F403 开发板搭建、GPIO/USART/TIM 基础驱动 | 基础工程模板 | LED闪烁、串口通信正常 |
| 第2周 | 接近开关输入捕获 + 限位开关检测 | 脉冲计数程序 | 手动触发接近开关，脉冲计数准确 |
| 第3周 | 接触器驱动电路 + 电机控制 + 互锁逻辑 | 电机正反转控制 | 两电机可独立控制正反转，互锁生效 |
| 第4周 | Flash EEPROM 模拟 + 掉电数据恢复 | 数据存储程序 | 断电后脉冲数恢复，误差≤1个脉冲 |

### 11.2 第二个月：核心逻辑与 HMI

| 周次 | 任务 | 交付物 | 验证标准 |
|------|------|--------|---------|
| 第5周 | 双柱同步控制算法 | 同步控制模块 | 手动制造误差，快柱停机等待功能正常 |
| 第6周 | 堵转检测 + 报警系统 + 声光预警 | 报警状态机 | 堵转0.5秒内准确报警 |
| 第7周 | 紧急操作模式 + 150mm脚部保护 | 安全降级逻辑 | A/B键组合紧急升降功能正常 |
| 第8周 | HMI UI 设计 + 通信协议 + LVGL 移植 | HMI 界面 | 三页面切换流畅，状态实时刷新 |

### 11.3 第三个月：IoT 集成与系统联调

| 周次 | 任务 | 交付物 | 验证标准 |
|------|------|--------|---------|
| 第9周 | UART IoT 模块识别 + ESP32/Air780E 适配 | 模块自动识别 | 插拔不同模块自动识别并通信 |
| 第10周 | MQTT 协议接入 + 云端对接 | IoT 上报程序 | 云端实时显示设备状态 |
| 第11周 | 全系统联调（频繁启停、断电、堵转模拟） | 联调报告 | 24小时连续运行无异常 |
| 第12周 | 文档整理 + 风险清单 | 技术文档 | 控制逻辑流程图、通信协议、测试报告 |

---

## 十二、风险评估与注意事项

### 12.1 高风险项

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 掉电计圈丢失 | **严重安全事故** | Flash双页冗余存储 + 超级电容保持最后1秒写入 |
| 接近开关漏脉冲 | 同步精度下降 | 输入捕获 + 硬件滤波 + 软件去抖 |
| 接触器切换死区不足 | 短路烧毁 | 硬件互锁 + 软件50ms死区 + 接触器机械寿命验证 |
| 150mm高度估算偏差 | 脚部保护失效 | 出厂标定满行程 + 定期校准 + 保守估算（偏大） |

### 12.2 需要实测确认的参数

| 参数 | 当前假设 | 确认方式 |
|------|---------|---------|
| 丝杆导程 | 10mm/转 | 实测：手动转丝杆一圈，测升降距离 |
| 满行程脉冲数 | 未知 | 从下限位到上限位全程计圈 |
| 堵转判定时间 | 500ms | 实测：放障碍物，从通电到堵转的时间 |
| 接近开关脉冲宽度 | 未知 | 示波器实测 |
| 接触器吸合时间 | 约20ms | 实测：用示波器测线圈到触点 |
| 电机类型 | 三相异步电机 | 查看电机铭牌确认功率、转速 |

### 12.3 与原 PLC 的差异对比

| 特性 | 原 PLC | STM32 方案 | 差异说明 |
|------|--------|-----------|---------|
| 上限位处理 | 串联在一个输入点 | 独立输入点 | 可实现分级保护 |
| 堵转判定 | 2秒 | 建议0.5秒 | 安全性提升 |
| 报警解除 | 断电重启 | 降至最低自动解除（OMCN） | 对标OMCN逻辑 |
| 单边控制 | 钥匙开关 | A/B键组合 | 更安全 |
| 数据存储 | 不确定 | Flash EEPROM | 掉电保护 |
| 150mm保护 | 外部信号 | MCU内部计圈 + 二次确认 | 更智能 |
| HMI显示 | 简陋 | LVGL 丰富界面 | 体验提升 |

---

## 十三、BOM 清单参考

| 部件 | 型号/规格 | 数量 | 参考单价 |
|------|----------|------|---------|
| STM32 主控板 | STM32F403VGT6 + 最小系统 | 1 | ~50元 |
| TFT 电阻屏 | 480x320 4.3寸 SPI/RGB | 1 | ~80元 |
| 光耦隔离板 | PC817 × 4 + 驱动电路 | 1 | ~10元 |
| 继电器板 | 24V 4路继电器 | 1 | ~15元 |
| 24V 开关电源 | 24V/5A | 1 | ~30元 |
| ESP32 子板（选配） | ESP32-WROOM | 1 | ~10元 |
| 4G Cat.1 子板（选配） | Air780E | 1 | ~20元 |
| 蜂鸣器 | 有源 24V | 1 | ~5元 |
| 指示灯 | LED 24V | 1 | ~3元 |
| 外壳 | 工业塑料/金属 | 1 | ~30元 |

---

## 十四、接线图参考

```
                        ┌──────────────────────┐
                        │    STM32F403VGT6     │
                        │                      │
  急停 SB0 ────────────→│ PC0                  │
  上限SQ1(柱1) ────────→│ PC1                  │
  上限SQ2(柱2) ────────→│ PC2                  │
  下限SQ3(柱1) ────────→│ PC3                  │
  下限SQ4(柱2) ────────→│ PC4                  │
  防碰杆SQ7 ──────────→│ PC5                  │
  上升按钮SB1 ────────→│ PC6                  │
  下降按钮SB2 ────────→│ PC7                  │
                        │                      │
  接近HE1(柱1) ────────→│ PA0 (TIM5_CH1)       │
  接近HE2(柱2) ────────→│ PA1 (TIM5_CH2)       │
  HMI A键 ────────────→│ PB0                  │
  HMI B键 ────────────→│ PB1                  │
  二次确认按钮 ───────→│ PB2                  │
                        │                      │
                        │ PD0 → KM1 (柱1上升)  │
                        │ PD1 → KM2 (柱1下降)  │
                        │ PD2 → KM3 (柱2上升)  │
                        │ PD3 → KM4 (柱2下降)  │
                        │ PD4 → HL 指示灯      │
                        │ PD5 → BZR 蜂鸣器     │
                        │                      │
                        │ PA9 (USART1_TX) → HMI│
                        │ PA10(USART1_RX) ← HMI│
                        │                      │
                        │ PA2 (USART2_TX) → IoT│
                        │ PA3 (USART2_RX) ← IoT│
                        └──────────────────────┘
```

> **注意：** 所有输入信号需经过 24V→3.3V 电平转换（光耦隔离或分压电阻），不可直接连接 PLC 的 24V 信号到 STM32。
