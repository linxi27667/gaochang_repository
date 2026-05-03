# STM32 丝杆举升机 — 纯 MCU 实现方案（基于 PLC 程序逆向）

> 通过解析现有 PLC 程序二进制（MAIN.wpg）+ 注释数据库 + OMCN 对标需求整理。

---

## 一、PLC 程序结构（从二进制文件逆向确认）

PLC 程序共 6 个功能段（从 MAIN.wpg 提取的中文注释标记）：

| 段号 | 注释标签 | 偏移地址 | 功能 |
|------|---------|---------|------|
| 1 | **初始化** | 0x0061 | 系统上电初始化，设置初始状态 |
| 2 | **计数** | 0x0111 | 双柱脉冲计数：C8(柱1)、C9(柱2)，下限位触发 RST 清零 |
| 3 | **数据比较程序段** | 0x0185 | 双柱计数值比较 → 误差超 4 圈时快柱停机等待 |
| 4 | **报警程序-霍尔故障** | 0x04c4 | 霍尔接近开关故障检测 → 触发报警 |
| 5 | **双稳态** | 0x057c | 上升/下降运行状态锁存（保持运行直到条件变化） |
| 6 | **赋值** | 0x05d8 | 输出映射：M 内部状态 → Y 物理输出 |

---

## 二、IO 分配总表

### 2.1 原 PLC 物理 I/O 映射

**原 PLC 输入 X0-X14：**

| PLC地址 | 信号 | 类型 | GPIO | 说明 |
|---------|------|------|------|------|
| X0 | 急停 SB0 | 常闭 | **PC0** | 程序中未出现（硬件急停回路），STM32仍需软件监控 |
| X1 | 上限位 SQ1（柱1） | 常闭 | **PC1** | 到上限停止柱1上升 |
| X2 | 上限位 SQ2（柱2） | 常闭 | **PC2** | 到上限停止柱2上升 |
| X3 | 下限位 SQ3（柱1） | 常开 | **PC3** | 到下限停止柱1下降 + **RST C8 清零计数** |
| X4 | 下限位 SQ4（柱2） | 常开 | **PC4** | 到下限停止柱2下降 + **RST C9 清零计数** |
| X5 | 螺母磨损 SQ5+SQ8 | 常开 | **PC5** | 并联接入，磨损触发→停机报警 |
| X7 | 上升按钮 SB1 | 常开 | **PC7** | 按下请求举升 |
| X8 | 上升按钮(副信号) | 常开 | **PC7** | PLC 中复制到多个输入，STM32 只需 1 个 |
| X9 | 下降按钮 SB2 | 常开 | **PC8** | 按下请求下降 |
| X10 | 下降驱动/记数清零 | 常开 | **PC8** | 同上，STM32 只需 1 个 |
| X11 | 转换开关 QS1 | 开关 | **PA15** | 0=同步自动 / 1=单边手动 |
| X12 | 霍尔接近 HE1（柱1） | NPN脉冲 | **PA0** | TIM5_CH1 输入捕获，每转1脉冲 |
| X13 | 霍尔接近 HE2（柱2） | NPN脉冲 | **PA1** | TIM5_CH2 输入捕获，每转1脉冲 |
| X14 | 预留输入 | — | — | 程序中引用但未接线 |

**原 PLC 输出 Y0-Y7：**

| PLC地址 | 信号 | GPIO | 说明 |
|---------|------|------|------|
| Y0 | 柱1上升 KM1 | **PD0** | 接触器吸合 → 柱1电机正转 |
| Y1 | 柱1下降 KM2 | **PD1** | 接触器吸合 → 柱1电机反转 |
| Y2 | 柱2上升 KM3 | **PD2** | 接触器吸合 → 柱2电机正转 |
| Y3 | 柱2下降 KM4 | **PD3** | 接触器吸合 → 柱2电机反转 |
| Y4 | 状态指示灯 | **PD4** | 正常运行亮 |
| Y5 | 蜂鸣器 | **PD5** | 预警/报警 |
| Y6 | 控制输出1 | **PD6** | PLC程序中"控制"，参与输出逻辑 |
| Y7 | 控制输出2 | **PD7** | PLC程序中"控制"，参与输出逻辑 |

### 2.2 PLC 内部设备映射（M 继电器 / T 定时器）

这些是 PLC 内部的软件设备，STM32 中用 struct 字段或枚举替代：

**M 内部继电器（共 10 个）：**

| M地址 | 功能推测 | STM32 替代方式 |
|-------|---------|---------------|
| M0 | 柱1报警标志 | `col1.alarm_flag` |
| M1 | 柱2报警标志 | `col2.alarm_flag` |
| M2 | 上升双稳态锁 | `state.rise_latched` |
| M3 | 下降双稳态锁 | `state.fall_latched` |
| M4 | 柱1同步锁定标志 | `col1.sync_blocked` |
| M5 | 柱2同步锁定标志 | `col2.sync_blocked` |
| M6 | 柱1电机运行标志 | `col1.motor_running` |
| M7 | 柱2电机运行标志 | `col2.motor_running` |
| M8 | 柱1二次确认标志 | `col1.secondary_confirmed` |
| M9 | 柱2二次确认标志 | `col2.secondary_confirmed` |

**T 定时器（共 6 个）：**

| T地址 | 功能 | 预设计时值 | STM32 替代方式 |
|-------|------|-----------|---------------|
| T2 | 同步恢复延时 | — | `HAL_GetTick()` 差值比较 |
| T3 | 下降同步延时 | — | `HAL_GetTick()` 差值比较 |
| T6 | 报警延时 | — | `HAL_GetTick()` 差值比较 |
| T7 | 报警解除延时 | — | `HAL_GetTick()` 差值比较 |
| T9 | 上升保持延时 | K18 (1.8s) | `HAL_GetTick()` 差值比较 |
| T10 | 下降保持延时 | K10 (1.0s) | `HAL_GetTick()` 差值比较 |

**C 计数器（共 2 个）：**

| C地址 | 功能 | STM32 替代方式 |
|-------|------|---------------|
| C8 | 柱1丝杆转数 | `col1.total_pulses` (int32_t) |
| C9 | 柱2丝杆转数 | `col2.total_pulses` (int32_t) |

### 2.3 完整 GPIO 速查表

| 序号 | GPIO | 方向 | 信号 | 原PLC | 模式 | 上/下拉 |
|------|------|------|------|-------|------|---------|
| 1 | PA0 | 输入 | 柱1 接近开关 HE1 | X12 | TIM5_CH1 捕获 | 下拉 |
| 2 | PA1 | 输入 | 柱2 接近开关 HE2 | X13 | TIM5_CH2 捕获 | 下拉 |
| 3 | PA2 | 输出 | USART2_TX (IoT) | — | AF | — |
| 4 | PA3 | 输入 | USART2_RX (IoT) | — | AF | 上拉 |
| 5 | PA9 | 输出 | USART1_TX (HMI) | — | AF | — |
| 6 | PA10 | 输入 | USART1_RX (HMI) | — | AF | 上拉 |
| 7 | PA15 | 输入 | 转换开关 QS1 | X11 | GPIO 输入 | 下拉 |
| 8 | PB0 | 输入 | HMI A 键 | — | GPIO 输入 | 下拉 |
| 9 | PB1 | 输入 | HMI B 键 | — | GPIO 输入 | 下拉 |
| 10 | PB2 | 输入 | 二次确认按钮 | — | GPIO 输入 | 下拉 |
| 11 | PC0 | 输入 | **急停 SB0** | X0 | GPIO 输入 | 上拉(常闭) |
| 12 | PC1 | 输入 | 上限位 SQ1 柱1 | X1 | GPIO 输入 | 上拉(常闭) |
| 13 | PC2 | 输入 | 上限位 SQ2 柱2 | X2 | GPIO 输入 | 上拉(常闭) |
| 14 | PC3 | 输入 | 下限位 SQ3 柱1 | X3 | GPIO 输入 | 下拉(常开) |
| 15 | PC4 | 输入 | 下限位 SQ4 柱2 | X4 | GPIO 输入 | 下拉(常开) |
| 16 | PC5 | 输入 | 螺母磨损 SQ5+SQ8 | X5 | GPIO 输入 | 下拉(常开) |
| 17 | PC6 | 输入 | 防碰杆 SQ7 | X6 | GPIO 输入 | 上拉(常闭) |
| 18 | PC7 | 输入 | 上升按钮 SB1 | X7/X8 | GPIO 输入 | 下拉(常开) |
| 19 | PC8 | 输入 | 下降按钮 SB2 | X9/X10 | GPIO 输入 | 下拉(常开) |
| 20 | PD0 | 输出 | **KM1 柱1上升** | Y0 | 推挽输出 | — |
| 21 | PD1 | 输出 | **KM2 柱1下降** | Y1 | 推挽输出 | — |
| 22 | PD2 | 输出 | **KM3 柱2上升** | Y2 | 推挽输出 | — |
| 23 | PD3 | 输出 | **KM4 柱2下降** | Y3 | 推挽输出 | — |
| 24 | PD4 | 输出 | 指示灯 HL | Y4 | 推挽输出 | — |
| 25 | PD5 | 输出 | 蜂鸣器 BZR | Y5 | 推挽/PWM | — |
| 26 | PD6 | 输出 | 控制1 | Y6 | 推挽输出 | — |
| 27 | PD7 | 输出 | 控制2 | Y7 | 推挽输出 | — |

---

## 三、PLC 程序逻辑逐段翻译

### 3.1 初始化段

```
功能：系统上电初始化
- 设置初始状态（所有输出 OFF）
- 初始化内部标志位
- PLC 中通过 K208 设定初始参数
```

**STM32 对应：**
```c
void System_Init(void) {
    HAL_Init();
    SystemClock_Config();  // 240MHz
    GPIO_Init();
    TIM5_InputCapture_Init();
    USART1_Init(115200);  // HMI
    USART2_Init(115200);  // IoT

    // 从 Flash 恢复数据
    NVM_Data_t saved;
    load_from_flash(&saved);
    if (saved.magic == 0x4C494654) {
        col1.total_pulses = saved.col1_pulses;
        col2.total_pulses = saved.col2_pulses;
    }

    // 所有输出初始为 OFF
    MOTOR_ALL_OFF();
}
```

### 3.2 计数段（C8/C9 计数器）

```
原 PLC 逻辑：
  X12(霍尔柱1) 脉冲 → C8 计数器 +1
  X13(霍尔柱2) 脉冲 → C9 计数器 +1
  X3(下限位柱1) 触发 → RST C8（柱1计数清零）
  X4(下限位柱2) 触发 → RST C9（柱2计数清零）
```

**STM32 对应：**
```c
// TIM5 输入捕获中断（PA0=柱1, PA1=柱2）
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        col1.total_pulses++;                    // C8++
        col1.last_pulse_tick = HAL_GetTick();   // 堵转检测用
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        col2.total_pulses++;                    // C9++
        col2.last_pulse_tick = HAL_GetTick();
    }
}

// 下限位检测（主循环中轮询）
void check_lower_limits(void) {
    if (HAL_GPIO_ReadPin(SQ3_COL1_PORT, SQ3_COL1_PIN) == GPIO_PIN_SET) {
        col1.total_pulses = 0;    // RST C8
        col1.direction = DIR_STOP;
    }
    if (HAL_GPIO_ReadPin(SQ4_COL2_PORT, SQ4_COL2_PIN) == GPIO_PIN_SET) {
        col2.total_pulses = 0;    // RST C9
        col2.direction = DIR_STOP;
    }
}
```

### 3.3 数据比较程序段（同步控制核心）

```
原 PLC 逻辑：
  比较 C8 和 C9 的计数值
  当 |C8 - C9| > 4 圈时：
    → 计数值大的那根柱子停止（快柱等慢柱）
  当 |C8 - C9| ≤ 4 圈时：
    → 两柱都恢复运行
```

**STM32 对应：**
```c
#define SYNC_THRESHOLD  4  // 4圈误差阈值，与PLC一致

void sync_check(void) {
    // 每 50ms 执行一次
    int32_t diff = col1.total_pulses - col2.total_pulses;

    if (diff > SYNC_THRESHOLD) {
        col1.sync_blocked = 1;  // 柱1快，停下来等柱2
        col2.sync_blocked = 0;
    } else if (diff < -SYNC_THRESHOLD) {
        col2.sync_blocked = 1;  // 柱2快，停下来等柱1
        col1.sync_blocked = 0;
    } else {
        // 误差在允许范围内，解除同步锁定
        col1.sync_blocked = 0;
        col2.sync_blocked = 0;
    }
}
```

### 3.4 报警程序-霍尔故障段

```
原 PLC 逻辑：
  M0/M1 = 柱1/柱2 报警标志
  霍尔传感器故障检测 → 设置报警标志
  报警时切断对应电机的输出
```

**STM32 对应：**
```c
void check_stall_alarm(void) {
    // 每 100ms 执行
    uint32_t now = HAL_GetTick();

    // 柱1：电机在运行，但超过阈值时间没收到脉冲
    if (col1.motor_running && !col1.sync_blocked) {
        if (now - col1.last_pulse_tick > STALL_TIMEOUT_MS) {
            enter_alarm(ALARM_STALL_COL1);  // 设置 M0
        }
    }

    // 柱2：同上
    if (col2.motor_running && !col2.sync_blocked) {
        if (now - col2.last_pulse_tick > STALL_TIMEOUT_MS) {
            enter_alarm(ALARM_STALL_COL2);  // 设置 M1
        }
    }
}
```

### 3.5 双稳态段

```
原 PLC 逻辑：
  M2 = 上升双稳态（按下上升按钮后保持运行状态，直到条件变化）
  M3 = 下降双稳态（同上）
  T9/T10 = 上升/下降保持延时
```

**STM32 对应：**
```c
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

// 双稳态：按下按钮后状态保持，松按钮不立即停
void process_state_machine(void) {
    switch (system_state) {
        case STATE_IDLE:
            if (KEY_RISE_PRESSED()) {
                buzzer_pre_rise();  // 蜂鸣2秒
                system_state = STATE_RISING;  // M2 SET
            } else if (KEY_FALL_PRESSED()) {
                system_state = STATE_FALLING;  // M3 SET
            }
            break;

        case STATE_RISING:
            // 保持上升，直到上限位/急停/堵转/松按钮
            if (!KEY_RISE_PRESSED()) {
                system_state = STATE_IDLE;  // M2 RST
                stop_all_motors();
            }
            break;

        case STATE_FALLING:
            if (!KEY_FALL_PRESSED()) {
                system_state = STATE_IDLE;  // M3 RST
                stop_all_motors();
            }
            break;
    }
}
```

### 3.6 赋值段（输出映射）

```
原 PLC 逻辑：
  Y0 = M6(柱1运行) AND NOT(M4柱1同步锁定) AND NOT(上限位) AND NOT(报警) → 柱1上升
  Y1 = M6(柱1运行) AND NOT(M4) AND NOT(下限位) AND NOT(报警) → 柱1下降
  Y2 = M7(柱2运行) AND NOT(M5柱2同步锁定) AND NOT(上限位) AND NOT(报警) → 柱2上升
  Y3 = M7(柱2运行) AND NOT(M5) AND NOT(下限位) AND NOT(报警) → 柱2下降
  Y4 = 状态指示灯
  Y5 = 蜂鸣器
  Y6/Y7 = 控制输出（参与互锁和状态指示）
```

**STM32 对应：**
```c
void update_motor_outputs(void) {
    // 柱1
    if (col1.motor_running && !col1.sync_blocked && !alarm_active) {
        if (col1.direction == DIR_RISE && !upper_limit_col1) {
            MOTOR1_RISE_ON();   // Y0=1
            MOTOR1_FALL_OFF();  // Y1=0
        } else if (col1.direction == DIR_FALL && !lower_limit_col1) {
            MOTOR1_RISE_OFF();  // Y0=0
            MOTOR1_FALL_ON();   // Y1=1
        } else {
            MOTOR1_ALL_OFF();   // Y0=0, Y1=0
        }
    } else {
        MOTOR1_ALL_OFF();
    }

    // 柱2（同上）
    if (col2.motor_running && !col2.sync_blocked && !alarm_active) {
        if (col2.direction == DIR_RISE && !upper_limit_col2) {
            MOTOR2_RISE_ON();
            MOTOR2_FALL_OFF();
        } else if (col2.direction == DIR_FALL && !lower_limit_col2) {
            MOTOR2_RISE_OFF();
            MOTOR2_FALL_ON();
        } else {
            MOTOR2_ALL_OFF();
        }
    } else {
        MOTOR2_ALL_OFF();
    }
}
```

---

## 四、功能清单

### 功能 1：基本升降控制
```
按下上升按钮 → 蜂鸣器响2秒 → 双柱接触器吸合 → 同步上升
按下下降按钮 → 双柱接触器吸合 → 同步下降
松开按钮 → 电机停止
```

### 功能 2：双柱同步控制（核心，对应"数据比较程序段"）
- 接近开关每检测到 1 个脉冲 = 丝杆转了 1 圈
- 实时比较两柱脉冲数（对应 PLC 中的 C8 和 C9 比较）
- **误差超过 4 圈**时：快柱停机等待，慢柱继续
- 误差回到 ≤4 圈时：快柱自动恢复
- **每 50ms 检查一次**

### 功能 3：限位保护

| 限位 | 动作 | 恢复 |
|------|------|------|
| 上限位 SQ1/SQ2 | 该柱停止上升 | 按下降可恢复 |
| 下限位 SQ3/SQ4 | 该柱停止下降 + **计圈清零**（RST C8/C9） | 自动 |
| 防碰杆 SQ7 | 全局急停，所有电机停止 | 排除障碍后手动复位 |

### 功能 4：堵转保护（霍尔故障报警）
- 电机运行但 **0.5 秒内** 没收到接近开关脉冲 → 停机报警
- 原 PLC 是 2 秒，建议缩短到 0.5 秒（需实测验证）
- **每 100ms 检查一次**

### 功能 5：螺母磨损保护
- 机械结构：螺母磨损极限 → 卡死电机 → 触发堵转报警
- 不需要额外传感器，复用堵转检测

### 功能 6：声光预警

| 场景 | 蜂鸣器 |
|------|--------|
| 举升前 | 鸣响 **2 秒** 后停 |
| 下降到 ~150mm | 持续长鸣 |
| 报警状态 | 响 0.5s → 停 0.5s → 循环 |
| 正常 | 静音 |

### 功能 7：150mm 脚部防压（二次确认，OMCN 对标新增）
```
下降到 ~150mm → 强制停机 → 蜂鸣器长鸣
→ 操作员检查脚下无障碍物
→ 再次按下下降按钮（二次确认 PB2）
→ 继续下降到底
```

### 功能 8：报警 + 紧急操作模式（对标 OMCN）

**触发条件**：堵转 / 防碰杆 / 螺母磨损

**紧急操作**：

| 按键组合 | 效果 |
|---------|------|
| 短按 A | 查看报警详情 |
| 长按 A+B 10秒 | 解锁进入紧急模式 |
| A + 下降 | 柱1单独下降 |
| B + 下降 | 柱2单独下降 |
| A + 上升 | 柱1单独上升 |
| B + 上升 | 柱2单独上升 |
| A+B + 下降 | 双柱同步下降 |
| A+B + 上升 | 双柱同步上升 |
| 下降到下限位 | 自动解除报警 → 回到空闲 |

### 功能 9：掉电数据保存

**必须保存**：
- 柱1/柱2计圈数（C8/C9 当前值）— **最关键，丢失 = 安全事故**
- 系统状态
- 报警代码
- 满行程标定值
- 累计举升次数

**方式**：STM32 内部 Flash 模拟 EEPROM，Sector 10/11 双页冗余，每 1 秒自动保存

---

## 五、系统状态机

```
[空闲 IDLE]
    ├─ 按上升 → [上升预备(蜂鸣2s)] → [上升中 RISING]
    ├─ 按下降 → [下降中 FALLING]
    └─ 急停/防碰杆 → [急停 EMERGENCY_STOP]

[上升中 RISING]          ← 对应 PLC 双稳态 M2
    ├─ 上限位到 → 该柱停
    ├─ 防碰杆触发 → 全局急停
    ├─ 误差>4圈 → [同步等待 SYNC_WAIT] → 误差恢复 → 回到上升
    ├─ 堵转 → [报警 ALARM]              ← 对应 PLC 报警程序段
    └─ 松按钮 → [空闲 IDLE]             ← M2 RST

[下降中 FALLING]         ← 对应 PLC 双稳态 M3
    ├─ 下限位到 → 该柱停 + 计圈清零      ← 对应 PLC RST C8/C9
    ├─ 到150mm → 停机 → 等二次确认 → 确认 → 继续
    ├─ 误差>4圈 → [同步等待]            ← 对应 PLC 数据比较段
    ├─ 堵转 → [报警]
    └─ 松按钮 → [空闲]                  ← M3 RST

[报警 ALARM]             ← 对应 PLC 报警程序-霍尔故障段
    ├─ 按A → 显示报警信息
    ├─ 长按A+B 10秒 → [紧急模式 EMERGENCY]
    └─ 下限位触发 → 自动解除 → [空闲]

[紧急模式 EMERGENCY]
    ├─ 组合键单独/同步升降
    └─ 下限位触发 → 自动解除 → [空闲]
```

---

## 六、数据结构定义

```c
// 单柱状态（对应 PLC 中每柱的 M 标志集合）
typedef struct {
    int32_t  total_pulses;       // C8/C9 计数器值
    uint32_t last_pulse_tick;    // 上次收到脉冲的系统tick（堵转检测）
    uint8_t  motor_running;      // M6/M7 电机运行标志
    uint8_t  direction;          // 0=停, 1=上升, 2=下降
    uint8_t  sync_blocked;       // M4/M5 同步锁定标志
    uint8_t  alarm_flag;         // M0/M1 报警标志
    uint8_t  secondary_confirmed; // M8/M9 二次确认标志
} Column_t;

Column_t col1 = {0}, col2 = {0};

// 系统状态
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
```

---

## 七、通信协议

### STM32 ↔ HMI（USART1）

**帧格式**：`[0xAA][长度][命令字][数据N][CRC8][0x55]`

| 命令字 | 方向 | 说明 |
|--------|------|------|
| 0x01 | 双向 | 心跳包（1s一次） |
| 0x02 | STM32→HMI | 状态上报（柱1脉冲+柱2脉冲+差值+状态+报警） |
| 0x03 | STM32→HMI | 报警上报 |
| 0x10 | HMI→STM32 | 按键事件（A键/B键/二次确认等） |
| 0x11 | HMI→STM32 | 参数设置（最大高度、上限位等） |

### STM32 ↔ IoT（USART2，选配）

- AT 指令方式，网络层由子板处理
- 上电自动检测模块类型
- MQTT 定时上报

---

## 八、需要实测确认的参数

| 参数 | 当前假设 | 怎么测 |
|------|---------|--------|
| 丝杆导程 | 10mm/转 | 手动转丝杆一圈，测升降距离 |
| 满行程脉冲数 | 未知 | 下限位→上限位全程计数 |
| 150mm 对应脉冲数 | 15 | 标定后：150 ÷ 导程 |
| 堵转判定时间 | 500ms | 放障碍物实测（PLC原值为2秒） |
| 接触器吸合时间 | ~20ms | 示波器测线圈到触点 |
| 接近开关脉冲宽度 | 未知 | 示波器实测 |
| 急停是否硬件回路 | 未知 | 查电气原理图确认 X0 是否接入 |

---

## 九、主程序框架

```c
int main(void) {
    System_Init();               // GPIO, TIM5, USART, Flash
    load_from_flash(&saved);     // 恢复计圈数据

    while (1) {  // 主循环，每 10ms 一次
        uint32_t now = HAL_GetTick();

        /* 1. 读所有输入 */
        read_all_inputs();

        /* 2. 急停最高优先级 */
        if (is_emergency()) { enter_emergency_stop(); }

        /* 3. 状态机处理（双稳态逻辑） */
        process_state_machine();

        /* 4. 同步控制检查（每50ms）← 对应 PLC 数据比较段 */
        if (now - last_sync >= 50) { sync_check(); last_sync = now; }

        /* 5. 堵转检测（每100ms）← 对应 PLC 报警程序段 */
        if (now - last_stall >= 100) { check_stall_alarm(); last_stall = now; }

        /* 6. 150mm脚部保护 */
        check_150mm_descent();

        /* 7. 紧急模式按键 */
        if (system_state == STATE_ALARM || system_state == STATE_EMERGENCY)
            emergency_key_handler();

        /* 8. 更新电机输出（对应 PLC 赋值段） */
        update_motor_outputs();

        /* 9. HMI通信（每200ms） */
        if (now - last_hmi >= 200) { hmi_send_status(); last_hmi = now; }

        /* 10. Flash保存（每1秒） */
        if (now - last_save >= 1000) { save_to_flash(); last_save = now; }

        delay_ms(10);
    }
}
```

---

## 十、文件/模块划分

```
STM32_Project/
├── Core/
│   ├── main.c              // 主循环 + 初始化
│   └── stm32f4xx_it.c      // TIM5 输入捕获中断
├── Drivers/
│   ├── gpio.c              // 27 路 GPIO
│   ├── tim.c               // TIM5 输入捕获
│   └── usart.c             // USART1/2
├── App/
│   ├── motor_control.c     // 电机方向 + 互锁 + 死区（对应PLC赋值段）
│   ├── sync_logic.c        // 双柱同步 4圈误差（对应PLC数据比较段）
│   ├── sensor.c            // 限位/按钮/接近开关读取
│   ├── alarm.c             // 报警状态机 + 紧急模式（对应PLC报警段）
│   ├── buzzer.c            // 蜂鸣器（2秒/长鸣/间断）
│   ├── flash_eeprom.c      // Flash 存储/恢复
│   └── state_machine.c     // 双稳态状态机（对应PLC双稳态段）
├── Comm/
│   ├── hmi_protocol.c      // HMI 串口协议
│   └── iot_at.c            // IoT AT指令（选配）
└── Inc/                    // 对应头文件
```

---

## 十一、开发计划

| 阶段 | 时间 | 做什么 | 验证标准 |
|------|------|--------|---------|
| **第1月** | 1-4周 | 环境搭建、GPIO驱动、TIM5计圈、限位检测、电机控制+互锁、Flash存储 | 脉冲计数准确、两电机可独立正反转互锁、断电数据恢复 |
| **第2月** | 5-8周 | 同步算法(4圈)、堵转检测、报警状态机、紧急模式、150mm保护、HMI界面 | 手动制造误差能自动补偿、堵转0.5秒报警、A/B键正常 |
| **第3月** | 9-12周 | IoT适配、全系统联调、24h烤机 | 频繁启停/断电/堵转均正常 |

---

## 十二、风险提醒

| 风险 | 严重度 | 措施 |
|------|--------|------|
| 掉电计圈丢失 | 🔴 致命 | Flash 双页冗余 + 确认板子有无超级电容 |
| 接触器切换短路 | 🔴 致命 | 硬件互锁(KM1/KM2常闭串联) + 软件50ms死区 |
| 150mm估算偏差 | 🟡 高 | 出厂标定满行程，保守估算（宁早勿晚） |
| 接近开关漏脉冲 | 🟡 高 | TIM 输入捕获 + 硬件滤波 + 软件去抖 |
| 堵转时间过短 | 🟡 高 | 实地测试，从 2 秒逐步缩短到 0.5 秒 |
