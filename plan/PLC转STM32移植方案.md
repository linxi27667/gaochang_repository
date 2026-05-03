# 丝杆举升机 PLC → STM32F407 移植方案 v2

> Hybrid Layered Architecture：APP 层驱动+逻辑，DRI 层创建任务调用。基于 Mitsubishi FX2N(C) PLC 程序分析 + OMCN 参考功能。

---

## 一、架构约定（混合模式）

### 1.1 分层规则

```
DRI 层 (Driver/Inc/*.h, Driver/Src/*.c)
  → 创建 FreeRTOS 任务 / 中断服务 / 对外封装
  → 调用 APP 层的函数

APP 层 (APP/Inc/*.h, APP/Src/*.c)
  → 硬件驱动 + 业务逻辑合一
  → 直接操作 GPIO、定时器、SPI 等 HAL 外设
```

### 1.2 命名规则

| 范围 | 规则 | 示例 |
|------|------|------|
| W25Q + SPI 相关 | **保留** `app_` / `bsp_` 前缀 | `app_w25qxx.c`, `bsp_w25qxx.c`, `app_spi.c`, `bsp_spi.c` |
| 其他所有新模块 | **不加**前缀，纯功能名 | `motor.h`, `sync.c`, `safety.h` |
| DRI 层 | `dri_` 前缀 | `dri_motor.c`, `dri_safety.c` |

### 1.3 现有文件变更

| 文件 | 变更 |
|------|------|
| `BSP/*` (bsp_w25qxx, bsp_spi) | **不动**，保持原样 |
| `APP/app_w25qxx.*, app_spi.*` | **不动**，保持原样 |
| `Driver/dri_debug.*` | **不动**，保持原样 |

---

## 二、项目现状

### 2.1 硬件平台

| 项目 | 当前值 |
|------|--------|
| MCU | STM32F407VET6 (Cortex-M4, 168MHz, 512KB Flash, 192KB SRAM) |
| RTOS | FreeRTOS (CMSIS-OS v1 wrapper) |
| NOR Flash | W25Q80 (1MB, JEDEC 0xEF4014) |
| 通信 | USART1, SPI1 |
| 定时器 | TIM2 (CH1/CH2 可用), TIM6 (HAL Tick) |
| 调试 | SEGGER RTT + EasyLogger V2.2.99 |

### 2.2 已实现

- SPI 总线抽象层 + W25Q 三层架构驱动
- 双扇区轮转持久化存储（12B struct, magic + XOR-CRC）
- Debug 任务（LED 闪烁 + 计数保存）
- FreeRTOS 基础调度

### 2.3 待实现

- 编码器脉冲捕获与计数
- 双柱同步算法（快等慢）
- 升降控制与方向切换
- 继电器/接触器 GPIO 驱动
- 防碰杆 + 堵转检测 + 下限位清零
- 二次下降保护 + 蜂鸣器
- 报警状态管理与紧急解锁
- 按键扫描 + OLED 显示
- 参数配置与 W25Q 存储

---

## 三、PLC 原程序 I/O 全部映射

### 3.1 输入信号

| PLC X | 信号 | 功能 | STM32 |
|-------|------|------|-------|
| X00 | 1# 接近开关 | 丝杆转1圈=1脉冲 | PA0, TIM2_CH1 输入捕获 |
| X01 | 2# 接近开关 | 同上 | PA1, TIM2_CH2 输入捕获 |
| X02 | 1# 防碰杆 | 臂下障碍物 | PB1, EXTI 下降沿 |
| X05 | 2# 防碰杆 | 同上 | PB2, EXTI 下降沿 |
| X06 | 1# 防碰备用 | 冗余检测 | PB3, EXTI |
| X07 | 2# 防碰备用 | 冗余检测 | PB4, EXTI |
| X10 | 下限位开关 | 到底触发，清零计数 | PB0, EXTI 上升沿 |

### 3.2 输出信号

| PLC Y | 信号 | STM32 | 驱动方式 |
|-------|------|-------|---------|
| Y00 | 1# 上升接触器 | PC0 | 推挽 + 光耦 + 继电器 |
| Y01 | 2# 上升接触器 | PC1 | 推挽 + 光耦 + 继电器 |
| Y04 | 1# 电机主接触器 | PC2 | 推挽 + 光耦 + 接触器 |
| Y05 | 2# 电机主接触器 | PC3 | 推挽 + 光耦 + 接触器 |
| Y06 | 刹车 | PC4 | 推挽 + 继电器(常闭) |
| Y07 | 反转(下降) | PC5 | 推挽 + 光耦 + 继电器 |

### 3.3 内部状态 → STM32 变量

| PLC | 含义 | STM32 全局变量 |
|-----|------|---------------|
| M/M1-M4 | 计数使能标志 | `g_col[0].counting_en`, `g_col[1].counting_en` |
| M5 | 清零计数器 | `g_sys.lower_limit_hit` |
| M6 | 防碰杆有效 | `g_sys.anti_collision` |
| M7 | 堵转检测计时 | `g_sys.obstacle_timer` |
| M8/M9 | 上升/下降 | `g_cmd.direction` (0停/1升/2降) |
| M10/M12 | 差值>允差 | `g_sync.exceeded` |
| M13/M14 | 快柱编号 | `g_sync.faster_col` |
| M20/M21 | 双柱模式 | `g_cfg.dual_mode` |
| M26/M27 | 独立柱控制 | `g_col[i].gs` |
| M30 | 防碰复位 | `g_sys.alarm_reset` |

### 3.4 定时器/计数器映射

| PLC | 含义 | STM32 |
|-----|------|-------|
| C235 | 1# 转数 | `g_col[0].pulse_count` (volatile) |
| C236 | 2# 转数 | `g_col[1].pulse_count` (volatile) |
| T00-T03 | 同步等待超时 | `g_col[i].wait_start_tick` + HAL_GetTick 比较 |
| T04/T05 | 堵转确认超时 | `g_sys.last_pulse_tick[i]` 比较 |
| D00-D05 | 配置参数 | `g_cfg.tolerance[]` 等 |

---

## 四、文件与模块设计

### 4.1 完整目录结构

```
code/f407vet6/
├── APP/
│   ├── Inc/
│   │   ├── app.h                    # 全局调试开关
│   │   ├── app_spi.h                # [保持] SPI总线声明
│   │   ├── app_w25qxx.h             # [保持] W25Q存储层API
│   │   ├── motor.h                  # 电机控制API
│   │   ├── encoder.h                # 脉冲计数API
│   │   ├── sync.h                   # 双柱同步API
│   │   ├── safety.h                 # 安全保护API
│   │   ├── hmi.h                    # 按键+显示+蜂鸣器API
│   │   └── config.h                 # 参数配置API+结构体
│   └── Src/
│       ├── app_spi.c                # [保持]
│       ├── app_w25qxx.c             # [保持]
│       ├── motor.c                  # 继电器驱动 + 升降逻辑
│       ├── encoder.c                # TIM2脉冲捕获 + 计数
│       ├── sync.c                   # 双柱同步算法
│       ├── safety.c                 # 安全监控/报警
│       ├── hmi.c                    # 按键扫描/OLED/蜂鸣器
│       └── config.c                 # 参数读写(W25Q)
├── BSP/
│   ├── Inc/
│   │   ├── bsp_spi.h                # [保持]
│   │   └── bsp_w25qxx.h             # [保持]
│   └── Src/
│       ├── bsp_spi.c                # [保持]
│       └── bsp_w25qxx.c             # [保持]
├── Driver/
│   ├── Inc/
│   │   ├── dri_debug.h              # [保持]
│   │   ├── dri_control.h            # 控制任务声明
│   │   ├── dri_safety.h             # 安全任务声明
│   │   └── dri_hmi.h                # HMI任务声明
│   └── Src/
│       ├── dri_debug.c              # [保持]
│       ├── dri_control.c            # Control_Task: 调用motor/sync
│       ├── dri_safety.c             # Safety_Task: 调用safety
│       └── dri_hmi.c                # HMI_Task: 调用hmi
```

### 4.2 模块职责明细

| 文件 | 层 | 职责 |
|------|----|------|
| `motor.h/c` | APP | GPIO初始化输出、继电器时序控制、刹车/方向、升降启停 |
| `encoder.h/c` | APP | TIM2输入捕获配置、EXTI中断、脉冲计数、脉冲间隔记录 |
| `sync.h/c` | APP | 差值计算、快慢柱判定、同步启停、超时报警触发 |
| `safety.h/c` | APP | 防碰杆检测、堵转判断、下限位清零、报警状态机 |
| `hmi.h/c` | APP | 按键消抖扫描、OLED驱动(I2C)、蜂鸣器PWM、菜单框架 |
| `config.h/c` | APP | 参数结构体定义、W25Q读写封装、默认值恢复 |
| `dri_control.c` | DRI | Control_Task(10ms周期): 调用motor升降 + sync同步 |
| `dri_safety.c` | DRI | Safety_Task(10ms周期): 调用safety各种检测 |
| `dri_hmi.c` | DRI | HMI_Task(50ms周期): 调用hmi按键+显示刷新 |

---

## 五、全局数据结构

### 5.1 config.h 统一定义

```c
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ===== 立柱数据结构 =====
typedef struct {
    volatile int32_t  pulse_count;         // 脉冲累计（ISR 更新）
    uint32_t          last_pulse_tick;     // 最后收到脉冲的 tick
    uint8_t           counting_en;         // 计数使能
    uint8_t           direction;           // 0=停 1=升 2=降
    uint8_t           motor_state;         // 电机状态
    uint32_t          wait_start_tick;     // 同步等待起始 tick
} column_t;

// ===== 控制指令 =====
typedef struct {
    uint8_t up;                            // 上升按钮
    uint8_t down;                          // 下降按钮
    uint8_t stop;                          // 急停
    uint8_t confirm;                       // 二次下降确认
    uint8_t direction;                     // 当前方向 0/1/2
} command_t;

// ===== 安全状态 =====
typedef struct {
    uint8_t  anti_collision;               // 防碰杆触发
    uint8_t  obstacle_detected;            // 堵转检测
    uint8_t  sync_timeout;                 // 同步超时
    uint8_t  nut_wear;                     // 螺母磨损
    uint8_t  secondary_descent_triggered;  // 二次下降触发
    uint8_t  secondary_descent_confirmed;  // 二次下降确认
    uint8_t  at_lower_limit;               // 已到下限位
    uint8_t  alarm_state;                  // 报警状态码
    uint8_t  alarm_reset;                  // 报警复位请求
    uint32_t last_pulse_tick[2];           // 最后脉冲时间
} safety_t;

// ===== 参数配置（持久化到 W25Q）=====
typedef struct {
    uint16_t header;                       // 0xA5A5 校验头
    uint16_t tolerance_up;                 // 上升允差（圈数, 默认4）
    uint16_t tolerance_down;               // 下降允差（圈数, 默认4）
    uint16_t obstacle_timeout_ms;          // 堵转判断时间（ms, 默认2000）
    uint16_t sync_wait_max_ms;             // 同步等待超时（ms, 默认10000）
    uint16_t anti_collision_debounce_ms;   // 防碰去抖（ms, 默认50）
    uint16_t secondary_descent_mm;         // 二次下降高度（mm, 默认150）
    uint8_t  dual_mode;                    // 0=独立 1=双柱联动
    uint8_t  auto_alarm_reset;             // 0=手动 1=自动复位
    uint8_t  screw_lead_mm;                // 丝杆导程（mm, 默认5）
    uint16_t crc16;                        // CRC16 校验
} config_t;

// ===== 全局变量声明 =====
extern column_t  g_col[2];
extern command_t g_cmd;
extern safety_t  g_safety;
extern config_t  g_cfg;

// 电机状态枚举
#define MOTOR_STOPPED       0
#define MOTOR_RUNNING       1
#define MOTOR_WAITING_SYNC  2
#define MOTOR_BLOCKED       3   // 被安全保护停掉

// 方向枚举
#define DIR_STOP    0
#define DIR_UP      1
#define DIR_DOWN    2

// 报警码
#define ALARM_NONE              0
#define ALARM_ANTI_COLLISION    1
#define ALARM_OBSTACLE          2
#define ALARM_SYNC_TIMEOUT      3
#define ALARM_NUT_WEAR          4
#define ALARM_EMERGENCY_STOP    5

#endif
```

### 5.2 全局变量实例化（config.c）

```c
#include "config.h"

column_t  g_col[2] = {0};
command_t g_cmd    = {0};
safety_t  g_safety = {0};
config_t  g_cfg    = {
    .header                    = 0xA5A5,
    .tolerance_up              = 4,
    .tolerance_down            = 4,
    .obstacle_timeout_ms       = 2000,
    .sync_wait_max_ms          = 10000,
    .anti_collision_debounce_ms = 50,
    .secondary_descent_mm      = 150,
    .dual_mode                 = 1,
    .auto_alarm_reset          = 0,
    .screw_lead_mm             = 5,
};
```

---

## 六、核心模块实现

### 6.1 encoder.c — 脉冲捕获

```c
#include "encoder.h"
#include "config.h"
#include "tim.h"

// TIM2 输入捕获初始化
void Encoder_Init(void)
{
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);   // PA0 → 1# 接近开关
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2);   // PA1 → 2# 接近开关
}

// TIM2 捕获中断回调
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;

    uint32_t tick = HAL_GetTick();

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        if (g_col[0].counting_en)
        {
            g_col[0].pulse_count++;
            g_col[0].last_pulse_tick = tick;
            g_safety.last_pulse_tick[0] = tick;
        }
    }
    else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
    {
        if (g_col[1].counting_en)
        {
            g_col[1].pulse_count++;
            g_col[1].last_pulse_tick = tick;
            g_safety.last_pulse_tick[1] = tick;
        }
    }
}

// 读转数（关中断保证原子性）
int32_t Encoder_GetCount(uint8_t col)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int32_t cnt = g_col[col].pulse_count;
    __set_PRIMASK(primask);
    return cnt;
}

// 清零计数器
void Encoder_ResetCount(uint8_t col)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_col[col].pulse_count = 0;
    __set_PRIMASK(primask);
}
```

### 6.2 motor.c — 电机控制

```c
#include "motor.h"
#include "config.h"

// 输出引脚定义（根据 CubeMX 实际分配）
#define MOTOR1_UP_PORT      GPIOC
#define MOTOR1_UP_PIN       GPIO_PIN_0
#define MOTOR1_MAIN_PORT    GPIOC
#define MOTOR1_MAIN_PIN     GPIO_PIN_2
#define MOTOR2_UP_PORT      GPIOC
#define MOTOR2_UP_PIN       GPIO_PIN_1
#define MOTOR2_MAIN_PORT    GPIOC
#define MOTOR2_MAIN_PIN     GPIO_PIN_3
#define BRAKE_PORT          GPIOC
#define BRAKE_PIN           GPIO_PIN_4
#define REVERSE_PORT        GPIOC
#define REVERSE_PIN         GPIO_PIN_5

// 刹车逻辑：常闭继电器，通电释放
// 停止 → HAL_GPIO_WritePin(BRAKE, SET)  = 释放刹车 = 电机自由
// 运行 → HAL_GPIO_WritePin(BRAKE, RESET) = 刹车抱紧 = 电机锁死

void Motor_Init(void)
{
    // 所有输出初始化为安全态
    Motor_StopAll();
    // 刹车上电抱紧
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, GPIO_PIN_RESET);
}

void Motor_Start(uint8_t col, uint8_t dir)
{
    if (dir == DIR_UP)
    {
        // 上升：主接触器 ON → 上升接触器 ON
        if (col == 0)
        {
            HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, GPIO_PIN_SET);
            osDelay(20);  // 接触器吸合延时
            HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, GPIO_PIN_SET);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, GPIO_PIN_SET);
        }
    }
    else if (dir == DIR_DOWN)
    {
        // 下降：反转继电器 ON → 主接触器 ON → 上升接触器 ON
        HAL_GPIO_WritePin(REVERSE_PORT, REVERSE_PIN, GPIO_PIN_SET);
        osDelay(20);
        if (col == 0)
        {
            HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, GPIO_PIN_SET);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, GPIO_PIN_SET);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, GPIO_PIN_SET);
        }
    }

    // 释放刹车
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, GPIO_PIN_SET);

    g_col[col].motor_state = MOTOR_RUNNING;
    g_col[col].direction = dir;
    g_col[col].counting_en = 1;
}

void Motor_Stop(uint8_t col)
{
    // 停止顺序：关上升接触器 → 关主接触器 → 关反转 → 抱紧刹车
    if (col == 0)
    {
        HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, GPIO_PIN_RESET);
        osDelay(10);
        HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, GPIO_PIN_RESET);
        osDelay(10);
        HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(REVERSE_PORT, REVERSE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, GPIO_PIN_RESET);

    g_col[col].motor_state = MOTOR_STOPPED;
    g_col[col].direction = DIR_STOP;
    g_col[col].counting_en = 0;
}

void Motor_StopAll(void)
{
    Motor_Stop(0);
    Motor_Stop(1);
}
```

### 6.3 sync.c — 双柱同步算法

```c
#include "sync.h"
#include "encoder.h"
#include "motor.h"
#include "config.h"

void Sync_Run(void)
{
    int32_t c0 = Encoder_GetCount(0);  // 原子读
    int32_t c1 = Encoder_GetCount(1);

    int32_t  diff;
    uint8_t  faster;

    if (c0 > c1) { diff = c0 - c1; faster = 0; }
    else         { diff = c1 - c0; faster = 1; }

    uint16_t tolerance = (g_cmd.direction == DIR_UP)
                         ? g_cfg.tolerance_up
                         : g_cfg.tolerance_down;

    if (diff > tolerance)
    {
        // 快柱超出允差 → 暂停快柱
        if (g_col[faster].motor_state == MOTOR_RUNNING)
        {
            Motor_Stop(faster);
            g_col[faster].motor_state = MOTOR_WAITING_SYNC;
            g_col[faster].wait_start_tick = HAL_GetTick();
        }
    }
    else
    {
        // 差值回到允差内 → 恢复
        for (int i = 0; i < 2; i++)
        {
            if (g_col[i].motor_state == MOTOR_WAITING_SYNC)
            {
                Motor_Start(i, g_cmd.direction);
            }
        }
    }

    // 超时检查
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < 2; i++)
    {
        if (g_col[i].motor_state == MOTOR_WAITING_SYNC)
        {
            if (now - g_col[i].wait_start_tick > g_cfg.sync_wait_max_ms)
            {
                Motor_StopAll();
                g_safety.sync_timeout = 1;
                g_safety.alarm_state = ALARM_SYNC_TIMEOUT;
            }
        }
    }
}
```

### 6.4 safety.c — 安全监控

```c
#include "safety.h"
#include "motor.h"
#include "encoder.h"
#include "hmi.h"

static uint8_t  debounce_anti_collision(uint8_t raw);
static uint32_t anti_collision_debounce_start = 0;

// EXTI 中断回调（合并在 gpio.c 或单独文件）
// 防碰杆 → PB1/PB2 EXTI 下降沿
// 下限位 → PB0 EXTI 上升沿

void Safety_LowerLimit_ISR(void)
{
    Encoder_ResetCount(0);
    Encoder_ResetCount(1);
    g_safety.at_lower_limit = 1;
}

void Safety_AntiCollision_ISR(void)
{
    // 硬件去抖：50ms 内重复触发忽略
    if (!debounce_anti_collision(1))
        return;

    Motor_StopAll();
    g_safety.anti_collision = 1;
    g_safety.alarm_state = ALARM_ANTI_COLLISION;
    Buzzer_On();
}

// 堵转检测：Safety_Task 每个周期调用
void Safety_CheckObstacle(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 2; i++)
    {
        if (g_col[i].motor_state != MOTOR_RUNNING) continue;

        if (now - g_safety.last_pulse_tick[i] > g_cfg.obstacle_timeout_ms)
        {
            Motor_StopAll();
            g_safety.obstacle_detected = 1;
            g_safety.alarm_state = ALARM_OBSTACLE;
            Buzzer_On();
        }
    }
}

// 二次下降保护：Control_Task 中下降时调用
void Safety_CheckSecondaryDescent(void)
{
    if (g_cmd.direction != DIR_DOWN) return;

    // 高度 ≈ 转数 × 导程
    int32_t height_mm = Encoder_GetCount(0) * g_cfg.screw_lead_mm;

    if (height_mm <= g_cfg.secondary_descent_mm
        && !g_safety.secondary_descent_confirmed)
    {
        Motor_StopAll();
        g_safety.secondary_descent_triggered = 1;
        Buzzer_Beep(2000);  // 蜂鸣 2s 提示
    }
}

// 报警复位（手动，类似 OMCN A+B 10s）
void Safety_ResetAlarm(void)
{
    if (g_safety.alarm_state == ALARM_NONE) return;

    g_safety.alarm_state        = ALARM_NONE;
    g_safety.anti_collision     = 0;
    g_safety.obstacle_detected  = 0;
    g_safety.sync_timeout       = 0;
    Buzzer_Off();
}

// 软件去抖
static uint8_t debounce_anti_collision(uint8_t raw)
{
    if (!raw) return 0;
    uint32_t now = HAL_GetTick();
    if (now - anti_collision_debounce_start < g_cfg.anti_collision_debounce_ms)
        return 0;
    anti_collision_debounce_start = now;
    return 1;
}
```

### 6.5 hmi.c — 人机交互

```c
#include "hmi.h"

#define BUZZER_PORT     GPIOB
#define BUZZER_PIN      GPIO_PIN_2

static uint32_t buzzer_off_tick = 0;

void Buzzer_On(void)          { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, SET); }
void Buzzer_Off(void)         { HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, RESET); }
void Buzzer_Beep(uint32_t ms) { Buzzer_On(); buzzer_off_tick = HAL_GetTick() + ms; }

void Buzzer_Poll(void)
{
    if (buzzer_off_tick && HAL_GetTick() > buzzer_off_tick)
    {
        Buzzer_Off();
        buzzer_off_tick = 0;
    }
}

// 按键扫描（A键/B键/上升键/下降键）
void HMI_KeyScan(void)
{
    // 读取按键 GPIO，更新 g_cmd 结构体
    // 含去抖逻辑（连续3次读到相同值确认）
}

// 显示刷新（OLED SSD1306 I2C）
void HMI_DisplayRefresh(void)
{
    // 显示转数、高度、报警信息、模式
}
```

### 6.6 config.c — 参数持久化

```c
#include "config.h"
#include "app_w25qxx.h"

#define CONFIG_FLASH_ADDR   0x00002000   // Sector 2 (避开 Slot A/B)

void Config_Load(void)
{
    config_t buf;
    if (W25Q_Read_Buffer(&W25Q_Flash, CONFIG_FLASH_ADDR,
                         (uint8_t*)&buf, sizeof(config_t)) != W25Q_OK)
        goto defaults;

    // 校验头 + CRC
    if (buf.header != 0xA5A5) goto defaults;
    uint16_t crc = Config_CRC16((uint8_t*)&buf, sizeof(config_t) - 2);
    if (crc != buf.crc16) goto defaults;

    g_cfg = buf;
    return;

defaults:
    // 保持默认值（已在定义时初始化）
    ;
}

void Config_Save(void)
{
    g_cfg.crc16 = Config_CRC16((uint8_t*)&g_cfg, sizeof(config_t) - 2);
    W25Q_Sector_Erase(&W25Q_Flash, CONFIG_FLASH_ADDR);
    W25Q_Page_Program(&W25Q_Flash, CONFIG_FLASH_ADDR,
                      (uint8_t*)&g_cfg, sizeof(config_t));
}
```

---

## 七、DRI 层任务实现

### 7.1 dri_control.c

```c
#include "dri_control.h"
#include "motor.h"
#include "sync.h"
#include "safety.h"
#include "config.h"

void Control_Task(void *pvParameters)
{
    while (1)
    {
        // 报警状态下不执行控制
        if (g_safety.alarm_state != ALARM_NONE)
        {
            osDelay(10);
            continue;
        }

        // 处理方向变更
        if (g_cmd.up && g_cmd.direction == DIR_STOP)
        {
            Motor_StartAll(DIR_UP);
            g_cmd.direction = DIR_UP;
        }
        else if (g_cmd.down && g_cmd.direction == DIR_STOP)
        {
            Motor_StartAll(DIR_DOWN);
            g_cmd.direction = DIR_DOWN;
        }
        else if (g_cmd.stop)
        {
            Motor_StopAll();
            g_cmd.direction = DIR_STOP;
        }

        // 运行时执行同步 + 安全
        if (g_cmd.direction != DIR_STOP)
        {
            Sync_Run();
            Safety_CheckSecondaryDescent();
        }

        osDelay(10);  // 10ms 控制周期
    }
}
```

### 7.2 dri_safety.c

```c
#include "dri_safety.h"
#include "safety.h"

void Safety_Task(void *pvParameters)
{
    while (1)
    {
        Safety_CheckObstacle();
        // 以后扩展：螺母磨损检测、电源监控

        osDelay(10);
    }
}
```

### 7.3 dri_hmi.c

```c
#include "dri_hmi.h"
#include "hmi.h"

void HMI_Task(void *pvParameters)
{
    while (1)
    {
        HMI_KeyScan();
        HMI_DisplayRefresh();
        Buzzer_Poll();

        osDelay(50);  // 50ms 刷新
    }
}
```

---

## 八、FreeRTOS 任务表

| 任务 | 优先级 | 栈(字) | 周期 | 入口文件 |
|------|--------|--------|------|---------|
| `Control_Task` | `osPriorityAboveNormal` | 512 | 10ms | dri_control.c |
| `Safety_Task` | `osPriorityAboveNormal` | 256 | 10ms | dri_safety.c |
| `HMI_Task` | `osPriorityNormal` | 512 | 50ms | dri_hmi.c |
| `Storage_Task` | `osPriorityLow` | 256 | 事件触发 | 直接调用 Config_Save |
| `Debug_Task` | `tskIDLE+1` | 512 | 250ms | dri_debug.c [已有] |

### 8.1 任务创建（freertos.c 中新增）

```c
osThreadId_t controlTaskHandle;
const osThreadAttr_t controlTask_attr = {
    .name = "control", .stack_size = 512, .priority = osPriorityAboveNormal,
};
osThreadId_t safetyTaskHandle;
const osThreadAttr_t safetyTask_attr = {
    .name = "safety", .stack_size = 256, .priority = osPriorityAboveNormal,
};
osThreadId_t hmiTaskHandle;
const osThreadAttr_t hmiTask_attr = {
    .name = "hmi", .stack_size = 512, .priority = osPriorityNormal,
};

// 在 MX_FREERTOS_Init 的 USER CODE BEGIN RTOS_THREADS 中:
controlTaskHandle = osThreadNew(Control_Task, NULL, &controlTask_attr);
safetyTaskHandle  = osThreadNew(Safety_Task,  NULL, &safetyTask_attr);
hmiTaskHandle     = osThreadNew(HMI_Task,     NULL, &hmiTask_attr);
```

---

## 九、中断优先级规划

| 中断 | 优先级(NVIC) | 说明 |
|------|-------------|------|
| TIM2 输入捕获 | 最高 (0,0) | 脉冲不能丢 |
| EXTI (防碰杆) | 高 (0,1) | 安全急停 |
| EXTI (下限位) | 高 (0,2) | 计数清零 |
| USART1 | 中 (1,0) | 日志不丢即可 |
| SysTick | 最低 (15,0) | HAL Tick |

> FreeRTOS 管理的中断优先级范围由 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 控制。TIM2 和 EXTI 必须在 FreeRTOS 管理范围之上（数字更小）。

---

## 十、生产级可靠性设计

### 10.1 看门狗

```c
// 使用 STM32 IWDG（独立看门狗）
// 在 main.c 中初始化：
void IWDG_Init(void)
{
    IWDG->KR = 0x5555;                       // 解除写保护
    IWDG->PR = 6;                            // 预分频 256 → 1.6s 超时
    IWDG->RLR = 1000;                        // 重装载
    IWDG->KR = 0xAAAA;                       // 刷新
    IWDG->KR = 0xCCCC;                       // 启动
}

// Safety_Task 中喂狗（关键：只有 Safety_Task 喂狗）
void Safety_Task(void *pvParameters)
{
    while (1)
    {
        Safety_CheckObstacle();
        IWDG->KR = 0xAAAA;                   // 喂狗
        osDelay(10);
    }
}
```

### 10.2 失效安全 (Fail-Safe)

| 场景 | 硬件行为 | 原因 |
|------|---------|------|
| MCU 死机 | 所有 GPIO 变高阻 → 继电器断开 → 电机停止 | GPIO 默认高阻态，继电器驱动电路设计为低电平吸合 |
| 看门狗复位 | MCU 重启 → 所有输出复位 → Motor_Init 初始化 | 软件启动即初始化所有输出为安全态 |
| 急停按下 | 硬件切断所有继电器电源（不经过 MCU） | 独立急停回路 |
| 电源跌落 | PVD 中断检测 → 立即保存关键数据到 W25Q | 1ms 内可写完 12B |

### 10.3 GPIO 失效安全电路

```
MCU_PC0 ──► 1kΩ ──► 光耦 PC817 ──► 三极管 ──► 继电器 ──► 接触器
                     │
                   10kΩ 上拉到 VCC (确保 MCU 死机时光耦不导通)
```

MCU 到 GPIO 输出必须通过**光耦隔离**。光耦输入端用 10kΩ 上拉到 VCC，确保 MCU 复位/死机时 GPIO 变高阻 → 光耦不导通 → 继电器释放。

### 10.4 电源监控

```c
// PVD 中断: 检测到电压跌落到 2.9V 以下
void PVD_IRQHandler(void)
{
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO))
    {
        // 紧急保存关键状态
        Motor_StopAll();
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_PVDO);
    }
}
```

### 10.5 继电器时序保护

所有继电器操作必须有时序保护：
1. 先合主接触器 → 20ms 后合方向继电器（防止飞弧）
2. 先断开方向继电器 → 10ms 后断主接触器（防止带载拉弧）
3. 反转方向必须先完全停止 → 200ms 延时 → 再启动反向

---

## 十一、实施顺序

### Phase 1：最小可验证系统（~5天）

| # | 任务 | 产出 |
|---|------|------|
| 1 | CubeMX 重新配置 GPIO/TIM2/EXTI | 新的 CubeMX 工程 |
| 2 | `encoder.c` + `motor.c` | 电机能受控启停，脉冲能计数 |
| 3 | `config.h` 全局结构体 | 编译通过 |
| 4 | `sync.c` 同步算法 | 两个模拟的接近开关信号能触发调平 |
| 5 | `dri_control.c` + Control_Task | 10ms 内跑完完整控制循环 |
| 6 | 硬件台架验证 | 接真实电机和接近开关测试 |

### Phase 2：安全保护（~3天）

| # | 任务 |
|---|------|
| 1 | `safety.c` 防碰杆 + 堵转检测 |
| 2 | 下限位清零 |
| 3 | 二次下降保护 |
| 4 | 蜂鸣器控制 |
| 5 | 报警状态机 + 紧急解锁流程 |

### Phase 3：人机交互（~3天）

| # | 任务 |
|---|------|
| 1 | 按键扫描 + 去抖 |
| 2 | OLED 驱动 + 画面框架 |
| 3 | 参数显示 + 配置菜单 |
| 4 | `config.c` 参数 W25Q 存取 |

### Phase 4：可靠性加固（~2天）

| # | 任务 |
|---|------|
| 1 | IWDG + PVD |
| 2 | 继电器失效安全验证 |
| 3 | 长时间运行测试 |

---

## 十二、风险清单

| 风险 | 影响 | 缓解 |
|------|------|------|
| 接近开关信号干扰 | 计数错误 → 同步紊乱 | 硬件 RC 滤波 + 软件去抖 |
| 继电器触点粘连 | 电机失控 | 继电器状态检测回路 |
| FreeRTOS 调度延迟 | 同步响应慢 | Control_Task 最高优先级 |
| W25Q 频繁擦写 | Flash 寿命耗尽 | 降低保存频率，仅在变化时写 |
| 光耦失效 | 输出失控 | 输出状态回读检测 |
| 中断嵌套深度过大 | 栈溢出 | 严格控制中断优先级层次 |

---

## 附录 A：HAL 中断回调统一管理

```c
// gpio.c or main.c — 所有 EXTI 回调的统一入口
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    switch (GPIO_Pin)
    {
        case LOWER_LIMIT_PIN:   Safety_LowerLimit_ISR();    break;
        case ANTI_COL1_PIN:
        case ANTI_COL2_PIN:     Safety_AntiCollision_ISR(); break;
        // 按键放在 HMI task 中轮询，不走中断
    }
}
```

## 附录 B：启动流程

```
main()
  → HAL_Init()
  → SystemClock_Config()
  → MX_GPIO_Init()           // GPIO 初始化
  → MX_TIM2_Init()           // 编码器捕获
  → MX_SPI1_Init()           // W25Q
  → IWDG_Init()              // 看门狗
  → SEGGER_RTT_Init()
  → elog_init()
  → App_W25Qxx_System_Init() // W25Q 初始化 + Load Debug counter
  → Encoder_Init()           // 启动 TIM2 捕获
  → Motor_Init()             // 初始化所有输出为安全态
  → Config_Load()            // 从 W25Q 加载参数
  → osKernelInitialize()
  → MX_FREERTOS_Init()       // 创建 task
  → osKernelStart()
```

## 附录 C：PLC 程序段对照

| PLC .wpg 段 | STM32 函数 | 所在文件 |
|------------|-----------|---------|
| 1#/2# 上升/下降计数 | `Encoder_Init`, `HAL_TIM_IC_CaptureCallback` | encoder.c |
| 1大於2 / 2大於1 | `Sync_Run` | sync.c |
| 停1柱/停2柱时间 | `Sync_Run` (超时检查) | sync.c |
| 防碰杆有效 | `Safety_AntiCollision_ISR` | safety.c |
| 计数器清零 | `Safety_LowerLimit_ISR` | safety.c |
| 报警状态延时 | `Safety_Task` + 状态机 | dri_safety.c |
| 双柱/单柱 | `g_cfg.dual_mode` 判断 | motor.c |
| 默认值/数值 | `g_cfg.tolerance_*` | config.c |

---

> **文档版本**: v2.0 | **编制日期**: 2026-05-03 | **架构模式**: Hybrid Layered Architecture (4-file per module, no BSP for new modules)
