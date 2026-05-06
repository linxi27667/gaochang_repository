# STM32F407VET6 丝杆举升机项目 — 架构交接日志

## 项目概述

- MCU: STM32F407VET6 (Cortex-M4, 168MHz)
- RTOS: FreeRTOS (CMSIS-OS v1 wrapper)
- Flash: W25Q80 (1MB, SPI1)
- 日志: EasyLogger (elog) + SEGGER RTT
- 工具链: MDK-ARM (Keil) / CMake + GCC
- CPU 类型: Mitsubishi FX2N(C) 兼容

---

## 目录结构（最终）

```
f407vet6/
├── APP/                      # 应用层（业务逻辑 + 硬件驱动合一）
│   ├── Inc/                  app_spi.h, app_w25qxx.h
│   │                         motor.h, key.h, encoder.h
│   │                         balance.h, safety.h
│   └── Src/                  app_spi.c, app_w25qxx.c
│                             motor.c, key.c, encoder.c
│                             balance.c, safety.c
├── BSP/                      # 板级支持包（不动）
│   ├── Inc/                  bsp_spi.h, bsp_w25qxx.h
│   └── Src/                  bsp_spi.c, bsp_w25qxx.c
├── Driver/                   # 驱动层（FreeRTOS 任务）
│   ├── Inc/                  dri_debug.h [已停用]
│   │                         dri_motor.h, dri_safety.h
│   │                         dri_key.h
│   └── Src/                  dri_debug.c [已停用]
│                             dri_motor.c, dri_safety.c
│                             dri_key.c
├── simulate/                 # PC 模拟测试（44项测试，50/50通过）
│   ├── stm32f4xx_hal.h/c     HAL 桩
│   ├── cmsis_os.h/c          FreeRTOS/CMSIS 桩
│   ├── bsp_w25qxx.h/c        W25Q 内存模拟
│   ├── main.c                16 个测试用例
│   └── Makefile              make && ./sim_test
├── Core/                     # CubeMX 核心
├── Drivers/                  # STM32 HAL 库
└── MDK-ARM/                  # Keil 工程
```

### 删除的文件

| 文件 | 说明 |
|------|------|
| `APP/Inc/app.h` | 见 main.h 各模块独立 DEBUG 宏 移入 `main.h` |
| `APP/Inc/config.h/c` | 数据结构分散到对应模块 |
| `APP/Inc/sync.h/c` | 重命名为 `balance` |
| `APP/Inc/storage.h/c` | 高度存取合并入 `app_w25qxx` |
| `APP/Inc/hmi.h/c` | 重命名为 `key` |
| `Driver/Inc/dri_hmi.h/c` | 重命名为 `dri_key` |

---

## 引脚分配表（2026-05-03）

所有引脚通过对应 .h 文件宏定义，用户可按实际接线修改宏即可，不需要改逻辑代码。

### 编码器输入（TIM2 输入捕获）

| 功能 | 引脚 | CubeMX 配置 | 宏定义位置 |
|------|------|------------|-----------|
| 1# 接近开关（丝杆转1圈=1脉冲） | PA0 | TIM2_CH1 Input Capture direct | — CubeMX 自动生成 |
| 2# 接近开关 | PA1 | TIM2_CH2 Input Capture direct | — CubeMX 自动生成 |

### 电机输出（继电器）

| 功能        | 引脚  | CubeMX 配置      | 宏定义                               |
| --------- | --- | -------------- | --------------------------------- |
| 1# 上升接触器  | PC0 | GPIO_Output PP | `motor.h`: `MOTOR1_UP_PORT/PIN`   |
| 2# 上升接触器  | PC1 | GPIO_Output PP | `motor.h`: `MOTOR2_UP_PORT/PIN`   |
| 1# 主接触器   | PC2 | GPIO_Output PP | `motor.h`: `MOTOR1_MAIN_PORT/PIN` |
| 2# 主接触器   | PC3 | GPIO_Output PP | `motor.h`: `MOTOR2_MAIN_PORT/PIN` |
| 刹车（常闭继电器） | PC4 | GPIO_Output PP | `motor.h`: `BRAKE_PORT/PIN`       |
| 反转（下降方向）  | PC5 | GPIO_Output PP | `motor.h`: `REVERSE_PORT/PIN`     |
|           |     |                |                                   |

### 安全输入（EXTI 中断）

| 功能          | 引脚  | CubeMX 配置               | 宏定义                                |
| ----------- | --- | ----------------------- | ---------------------------------- |
| 下限位开关（到底触发） | PB0 | GPIO_EXTI, Rising edge  | `safety.h`: `LOWER_LIMIT_PORT/PIN` |
| 1# 防碰杆      | PB1 | GPIO_EXTI, Falling edge | `safety.h`: `COLLISION_1_PORT/PIN` |
| 2# 防碰杆      | PB2 | GPIO_EXTI, Falling edge | `safety.h`: `COLLISION_2_PORT/PIN` |

### 按键输入（轮询扫描，不用 EXTI）

| 功能 | 引脚 | CubeMX 配置 | 宏定义 |
|------|------|------------|--------|
| 上升键 | PB12 | GPIO_Input Pull-up | `key.h`: `BUTTON_UP_PORT/PIN` |
| 下降键 | PB13 | GPIO_Input Pull-up | `key.h`: `BUTTON_DOWN_PORT/PIN` |
| 停止键 | PB14 | GPIO_Input Pull-up | `key.h`: `BUTTON_STOP_PORT/PIN` |
| 确认键（二次下降） | PB15 | GPIO_Input Pull-up | `key.h`: `BUTTON_CONFIRM_PORT/PIN` |

### 蜂鸣器

| 功能 | 引脚 | CubeMX 配置 | 宏定义 |
|------|------|------------|--------|
| 蜂鸣器输出 | PB8 | GPIO_Output PP | `key.h`: `BUZZER_PORT/PIN` |

---

## CubeMX 配置步骤

打开 `Ball_And_Plant_System.ioc`：

### 1. TIM2 输入捕获
- PA0 → TIM2_CH1 (Input Capture direct mode)
- PA1 → TIM2_CH2 (Input Capture direct mode)
- Prescaler: 80-1（1MHz 计数频率）
- Counter Period: 65535-1
- NVIC: 勾选 TIM2 global interrupt, Preemption=0, Sub=0

### 2. PC0-PC5 输出
- 全部设为 GPIO_Output PP, Low, No pull

### 3. PB8 输出（蜂鸣器）
- GPIO_Output PP, Low, No pull

### 4. PB0-PB2 EXTI 输入
- PB0: GPIO_EXTI, Rising edge trigger, Preemption=1
- PB1: GPIO_EXTI, Falling edge trigger, Preemption=1
- PB2: GPIO_EXTI, Falling edge trigger, Preemption=1

### 5. PB12-PB15 按键输入
- GPIO_Input, Pull-up（不上拉到外部按键就是高电平）

---

## W25Q 三层架构

```
APP (app_w25qxx.c)      ← 存储调度、dual-slot 轮转、高度存取
  ↓
BSP (bsp_w25qxx.c)      ← SPI 命令集：Read/Erase/Program/WaitBusy
  ↓
BSP (bsp_spi.c)         ← SPI 总线抽象 (Select / Deselect)
  ↓
APP (app_spi.c)         ← 硬件绑定 (hspi1 + PA4-CS)
```

### W25Q 存储布局

| 地址 | 用途 | 大小 |
|------|------|------|
| 0x00000000 | Slot A（旧 debug counter 轮转，无任务写入） | 4KB (Sector 0) |
| 0x00001000 | Slot B（同上） | 4KB (Sector 1) |
| 0x00002000 | **高度存储** `w25q_height_t` | 4KB (Sector 2) |

### 全局配置参数（w25q_config_t）

定义在 `app_w25qxx.h`，变量 `g_config` 在 `app_w25qxx.c` 中实例化：

```c
typedef struct {
    uint16_t header;                   // 0xA5A5
    uint16_t tolerance_up;             // 上升允差（脉冲数，默认4）
    uint16_t tolerance_down;           // 下降允差（脉冲数，默认4）
    uint16_t stall_timeout_ms;         // 堵转检测（默认2000ms）
    uint16_t balance_wait_max_ms;      // 平衡超时（默认10000ms）
    uint16_t collision_debounce_ms;    // 防碰去抖（默认50ms）
    uint16_t secondary_descent_pulses; // 二次下降触发脉冲（默认30）
    uint8_t  dual_column_mode;         // 0=单柱 1=双柱联动（默认1）
    uint8_t  screw_lead_mm;            // 丝杆导程（默认5mm）
    uint16_t max_pulses;               // 上限位脉冲数（默认2000 ≈ 10米）
    uint16_t crc16;
} w25q_config_t;
```

### 高度存储结构（w25q_height_t）

```c
typedef struct {
    uint32_t magic;       // 0x48494748 ("HIGH")
    int32_t  heights[2];  // 两个立柱脉冲计数
    uint32_t crc;         // CRC32
} w25q_height_t;
```

存储在 Sector 2 (0x00002000)，每 5 秒保存一次。原子读取（关中断保护）。

---

## 中断优先级

| 中断 | Preempt | Sub | 说明 |
|------|---------|-----|------|
| TIM2 输入捕获 | 0 | 0 | 脉冲计数不丢 |
| EXTI0 (下限位) | 1 | 0 | 清零计数器 |
| EXTI1/2 (防碰杆) | 1 | 0 | 急停 |
| TIM6 (HAL Tick) | 15 | 0 | 系统 tick |
| FreeRTOS 管理范围 | ≥5 | — | 可调用 FromISR API |

---

## FreeRTOS 任务表

| 任务名 | 优先级 | 栈(字) | 周期 | 源码 | 说明 |
|--------|--------|--------|------|------|------|
| `control` | AboveNormal | 512 | 10ms | `dri_motor.c` | 按钮响应、启停电机、平衡算法、限位保护 |
| `safety` | AboveNormal | 256 | 10ms | `dri_safety.c` | 堵转检测、告警复位、二次下降确认 |
| `key` | Normal | 512 | 50ms | `dri_key.c` | 按键扫描去抖、蜂鸣器轮询 |
| `defaultTask` | Normal | 512 | 1ms | `freertos.c` | 空闲任务 |
| ~~`debug`~~ | ~~IDLE+1~~ | ~~512~~ | ~~停用~~ | ~~`dri_debug.c`~~ | ~~LED 闪烁已注释~~ |

---

## 核心模块说明

### motor.h/c — 电机控制（继电器时序）

- **上升**: 主接触器合 → 20ms → 上升接触器合 → 刹车释放
- **下降**: 反转继电器合 → 20ms → 主接触器合 → 20ms → 上升接触器合 → 刹车释放
- **停止**: 上升接触器断 → 10ms → 主接触器断 → 反转断 → 刹车抱紧
- **紧急停机(ISR安全)**: `Motor_Stop_All_Immediate()` — 无延时，ISR 中直接调用
- **失效安全**: MCU 死机→GPIO高阻→光耦不导通→继电器全断→刹车抱紧

### key.h/c — 按键 + 蜂鸣器

- 4 个按键轮询扫描，3 次去抖（每次间隔 1ms `HAL_Delay`）
- `g_command` 记录按键状态和当前方向
- 蜂鸣器支持 `Beep(ms)` 定时关闭、持续 ON/OFF

### encoder.h/c — 脉冲捕获

- TIM2_CH1(PA0) + TIM2_CH2(PA1) 输入捕获 ISR
- ISR 中仅做 `g_column[i].pulse_count++` + 记录 `last_pulse_tick`
- `Encoder_Get_Count()` / `Encoder_Reset_Count()` 关中断原子保护
- 高度换算: `height_mm = pulse_count × g_config.screw_lead_mm`

### balance.h/c — 双柱平衡算法

- 每 10ms 计算两柱脉冲差值 `|c0 - c1|`
- 快柱超出允差(`tolerance_up/down`) → 暂停快柱
- 差值回到允差内 → 恢复快柱
- 超时(`balance_wait_max_ms`) → 全部停机 + `ALARM_BALANCE_TIMEOUT`

### safety.h/c — 安全保护

| 保护        | 触发                                            | 动作                             |
| --------- | --------------------------------------------- | ------------------------------ |
| **上限位**   | `pulse_count >= max_pulses` 且方向向上             | 停机 + `ALARM_UPPER_LIMIT`       |
| **下限位**   | `pulse_count <= 0` 且方向向下                      | 停机 + `at_lower_limit`          |
| **防碰杆**   | PB1/PB2 EXTI 下降沿 + 50ms去抖                     | 紧急停机 + `ALARM_COLLISION` + 蜂鸣器 |
| **堵转**    | 运行中 `stall_timeout_ms` 无脉冲                    | 停机 + `ALARM_STALL` + 蜂鸣器       |
| **下限位开关** | PB0 EXTI 上升沿                                  | 清零两个计数器                        |
| **二次下降**  | 下行时 `pulse_count <= secondary_descent_pulses` | 停机 + 需按确认键                     |
| **告警复位**  | 告警态按下停止键                                      | 清告警 + 蜂鸣器关 + `DIR_STOP`        |

---

## 审查修复记录（2026-05-03）

### 审查发现的致命问题

| # | 问题 | 严重 | 位置 | 修复 |
|---|------|------|------|------|
| 1 | `Safety_EXTI_Handler`(ISR) 调用 `osDelay(10)` → HardFault | 致命 | `safety.c:46` | 新建 `Motor_Stop_All_Immediate()` 无延时版本 |
| 2 | `last_pulse_tick=0` 导致上电瞬间堵转误报 | 致命 | `safety.c:61` | `Motor_Start` 启动时初始化 `last_pulse_tick = HAL_GetTick()` |
| 3 | 堵转检测在 Control_Task 和 Safety_Task 重复调用 | 重要 | `dri_motor.c:56` | 只保留 Safety_Task 中调用 |
| 4 | `Height_Save` 非原子读 `pulse_count` | 重要 | `app_w25qxx.c:222` | 加 `__disable_irq` / `__set_PRIMASK` 保护 |
| 5 | 无告警复位入口 | 重要 | `safety.c:72` | Safety_Task: 告警态按停止键复位 |
| 6 | 急停键按住反复操作继电器 | 重要 | `dri_motor.c:26` | 加条件 `direction != DIR_STOP` |
| 7 | 上升/下降键无互斥 | 重要 | `dri_motor.c:34-51` | 上升需 `!button_down`，下降需 `!button_up` |
| 8 | 二次下降保护未实际触发 | 重要 | — | 新增 `Safety_Check_Secondary_Descent()` |
| 9 | 告警复位后 `direction` 未清零 | 重要 | `safety.c:100` | `Safety_Alarm_Reset` 追加 `DIR_STOP` |
| 10 | ISR 变量无 `volatile` 修饰 | 重要 | `safety.h:8-16` | `collision_triggered`, `at_lower_limit`, `alarm_state`, `last_pulse_tick[2]` 加 volatile |

### 原项目已修复 Bug

| # | 问题 | 修复 |
|---|------|------|
| Bug 1&2 | W25Q_OK/W25Q_ERR 布尔反转 | 全部改为显式 `== W25Q_OK` / `!= W25Q_OK` |
| Bug 3 | SPI 接收时 TX 缓冲区越界 | 改用 `HAL_SPI_Receive()` |

---

## 实物调试记录（2026-05-05）v2.2

### 变更总览

| 分类 | 变更 | 涉及文件 |
|------|------|---------|
| 点动模式 | 锁存→按住跑松手停，双键冲突即停 | `dri_motor.c` |
| 按键 | KEY_前缀+key_t结构体+状态机+两帧确认消抖(40ms) | `key.h/c` |
| 编码器 | 模拟脉冲(每50ms)，6mm/脉冲，5:4两柱偏差 | `dri_debug.c/h` |
| 安全 | 上限位/下限位不设告警只停，防碰模拟(500mm) | `safety.c/h`, `dri_debug.c` |
| 平衡 | Motor_Pause(单柱暂停不关REVERSE/BRAKE)，下降方向反向判定 | `motor.c/h`, `balance.c` |
| 存储 | 高度变化且>5s才写Flash | `dri_motor.c` |
| 日志 | mm单位+简洁英文+蜂鸣告警级+A/Assert红 | 全模块 |
| 枚举 | motor_state_t, direction_t, alarm_t, key_state_t | `motor.h`, `safety.h`, `key.h` |
| 任务 | `dri_motor.c` 主循环拆6个函数 | `dri_motor.c` |

### 当前配置

```c
// main.h
MAX_HEIGHT_MM     4000    // 最大高度 4m（可改）
MAX_PULSES        666     // 4000/6
COLLISION_ENABLE  0       // 真机改1
SECONDARY_DESCENT_ENABLE 0   // 真机改1

// dri_debug.h
SIM_PULSE_INTERVAL_MS   50
SIM_COLLISION_HEIGHT_MM 500
```

### 控制任务结构

```
Control_Task 10ms:
  Alarm_Handle()        // 告警：碰撞→下降键复位
  双键冲突检测            // UP+DOWN同时按→停机
  Jog_Release_Check()    // 松手停
  Jog_Start_Check()      // 按住启+限位守卫(上限阻升/下限阻降)
  Running_Update()       // 编码器→高度日志→平衡→防碰→限位
  Flash_Save_If_Needed() // 高度变化+>5s才写
```

### 平衡方向判定

| 方向 | 快柱= | 暂停 |
|------|--------|------|
| 上升 | 脉冲多(升得高) | 等慢柱升上来 |
| 下降 | 脉冲少(降得多) | 等慢柱降下来 |
| 偏差 | >4脉冲(24mm) | Motor_Pause(只停主+上，保留反转) |

### 关键修复

| Bug | 根因 | 修复 |
|-----|------|------|
| 上电4继电器全吸合 | 低电平继电器，RELAY_ON定义反了 | 反转 RELAY_ON=RESET |
| 堵转误报2秒必触发 | Sim_Encoder不更新g_safety.last_pulse_tick | 同步更新 |
| 平衡暂停的柱子还在涨脉冲 | Sim_Encoder不管motor_state | 加 MOTOR_RUNNING 判断 |
| Motor_Stop关全系统反转 | 平衡暂停单柱时把另一柱也停了 | 新建 Motor_Pause |
| 二次下降保护死循环 | 无确认键，永远通不过 | SECONDARY_DESCENT_ENABLE=0 |
| 幽灵按键反复触发 | PB12-15 CubeMX配成GPIO_NOPULL浮空 | CubeMX改为GPIO_PULLUP + 两帧消抖 |

### 删除的冗余字段 (safety_state_t)

| 删除 | 替代 |
|------|------|
| collision_triggered | `alarm == ALARM_COLLISION` |
| stall_detected | `alarm == ALARM_STALL` |
| balance_timeout | `alarm == ALARM_BALANCE_TIMEOUT` |
| alarm_state(uint8_t) | `alarm(alarm_t枚举)` |

---

## OMCN 控制逻辑对比（2026-05-05）

| OMCN 功能需求 | STM32 实现 | 匹配 |
|--------------|-----------|:---:|
| 双柱平衡（4圈误差） | `Balance_Run` tolerance=4脉冲 | ✅ |
| 遇障碍→堵转停机报警 | `Safety_Check_Stall` 2秒无脉冲触发 | ✅ |
| 防碰杆→紧急停机 | `Sim_Collision_Check`(调试) + `COLLISION_ENABLE`(真机) | ✅ |
| 下限位→清零计数器 | `Safety_Check_Lower_Limit` → `Encoder_Reset_Count` 双柱归零 | ✅ |
| 上限位→停止 | `Safety_Check_Upper_Limit` → `at_upper_limit` 标记 | ✅ |
| 150mm二次下降保护 | `SECONDARY_DESCENT_ENABLE`（调试关，真机开） | ⚠️ |
| 报警状态→解锁模式 | 碰撞按下降键复位（OMCN需A+B长按10秒） | ⚠️ 更简单 |
| 下降前蜂鸣器2秒 | 当前500ms | ⚠️ |
| 独立单柱升降 | `Motor_Start(col)` + `Motor_Pause(col)` 支持 | ✅ |
| Flash存储高度 | W25Q 变化+>5s写入，上电自动恢复 | ✅ |
| 显示屏/物联网 | 不适用（需第三方合作开发） | ❌ |
