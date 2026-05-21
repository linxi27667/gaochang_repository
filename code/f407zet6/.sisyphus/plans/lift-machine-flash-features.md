# 举升机Flash存储、防撞杆、双键强制下降

## TL;DR

> **Quick Summary**: 在STM32F407举升机项目中启用W25Q Flash高度持久化、实现4个防撞杆的自动限位检测、添加双键强制下降功能，并采用"停止时立即保存"策略。
>
> **Deliverables**:
> - W25Q CS引脚修正（PB6）
> - 防撞杆检测逻辑（4个PE5/6/7/8）
> - 双键强制下降功能
> - 停止时立即保存高度
> - main.c启用Flash初始化
>
> **Estimated Effort**: Medium
> **Parallel Execution**: YES - 2 waves

---

## Context

### Original Request
1. 添加防撞杆 Left_Up_Safety(PE5)、Right_Up_Safety(PE6)、Left_Down_Safety(PE7)、Right_Down_Safety(PE8)
2. 撞到防撞杆时自动设置高度为最大(2000mm)或最低(0mm)
3. SPI/CS引脚已变更，需要修改配置
4. 新增双键强制下降功能（绕过下限位，但防撞杆仍有效）
5. 运动→停止时立即保存高度到W25Q

### Key Decisions
- 防撞杆触发时：停止电机 + 设置pulse_count为极值
- 双键强制下降：绕过`at_lower_limit`，但防撞杆最高优先级仍阻止下降
- 保存策略：只在停止时保存，运动中不保存

---

## Work Objectives

### Core Objective
启用Flash高度持久化 + 防撞杆自动限位 + 双键强制下降

### Must Have
- CS引脚改为PB6（app_spi.h）
- 防撞杆检测：高电平=撞到，自动设置pulse_count极值
- 双键强制下降：UP+DOWN同时按→绕过at_lower_limit→但防撞杆阻止
- 停止时立即保存：`App_W25Qxx_Height_Save_If_Needed()`在`Motor_Stop_All()`后调用

### Must NOT Have
- 运动中不保存高度（只有停止时）
- 双键强制下降不能绕过防撞杆（防撞杆最高优先级）

---

## TODOs

- [ ] 1. 修改app_spi.h CS引脚

  **What to do**:
  - 修改 `APP/Inc/app_spi.h` 第9-10行
  - `SPI_CS_PORT` 从 `GPIOA` 改为 `GPIOB`
  - `SPI_CS_PIN` 从 `GPIO_PIN_4` 改为 `GPIO_PIN_6`

  **Must NOT do**:
  - 不修改其他SPI配置

  **References**:
  - `APP/Inc/app_spi.h:9-10` - CS引脚定义
  - `Core/Inc/main.h:102-103` - W25Q_CS_Pin=GPIO_PIN_6, W25Q_CS_GPIO_Port=GPIOB

  **Acceptance Criteria**:
  - [ ] `app_spi.h` 中 `SPI_CS_PORT=GPIOB`, `SPI_CS_PIN=GPIO_PIN_6`

  **Commit**: YES
  - Message: `fix(spi): correct W25Q CS pin to PB6`
  - Files: `APP/Inc/app_spi.h`

- [ ] 2. 实现防撞杆检测逻辑

  **What to do**:
  - 修改 `APP/Inc/safety.h`：在 `safety_state_t` 结构体中添加：
    ```c
    volatile uint8_t  left_up_collision;    // 左上升防撞杆
    volatile uint8_t  right_up_collision;   // 右上升防撞杆
    volatile uint8_t  left_down_collision;  // 左下降防撞杆
    volatile uint8_t  right_down_collision; // 右下降防撞杆
    ```
  - 添加函数声明：`void Safety_Check_Collision(void);`
  - 修改 `APP/Src/safety.c`：实现 `Safety_Check_Collision()`:
    ```c
    void Safety_Check_Collision(void)
    {
        // 读取4个防撞杆GPIO（高电平=撞到）
        g_safety.left_up_collision   = HAL_GPIO_ReadPin(Left_Up_Safety_GPIO_Port, Left_Up_Safety_Pin);
        g_safety.right_up_collision  = HAL_GPIO_ReadPin(Right_Up_Safety_GPIO_Port, Right_Up_Safety_Pin);
        g_safety.left_down_collision = HAL_GPIO_ReadPin(Left_Down_Safety_GPIO_Port, Left_Down_Safety_Pin);
        g_safety.right_down_collision= HAL_GPIO_ReadPin(Right_Down_Safety_GPIO_Port, Right_Down_Safety_Pin);

        // 上升防撞杆：停止上升 + 设置最大高度
        if (g_safety.left_up_collision || g_safety.right_up_collision) {
            if (g_command.direction == DIR_UP) {
                Motor_Stop_All();
                g_command.direction = DIR_STOP;
                g_safety.at_upper_limit = 1;
            }
            // 设置脉冲计数为最大值
            if (g_safety.left_up_collision)  g_column[0].pulse_count = (int32_t)g_config.max_pulses;
            if (g_safety.right_up_collision) g_column[1].pulse_count = (int32_t)g_config.max_pulses;
        }

        // 下降防撞杆：停止下降 + 设置最小高度
        if (g_safety.left_down_collision || g_safety.right_down_collision) {
            if (g_command.direction == DIR_DOWN) {
                Motor_Stop_All();
                g_command.direction = DIR_STOP;
                g_safety.at_lower_limit = 1;
            }
            // 设置脉冲计数为0
            if (g_safety.left_down_collision)  g_column[0].pulse_count = 0;
            if (g_safety.right_down_collision) g_column[1].pulse_count = 0;
        }
    }
    ```
  - 在 `Safety_Running_Update()` 中调用 `Safety_Check_Collision()`

  **Must NOT do**:
  - 不使用EXTI中断（使用轮询方式）

  **References**:
  - `Core/Inc/main.h:80-87` - 防撞杆引脚定义
  - `APP/Inc/safety.h:14-22` - safety_state_t 结构体
  - `APP/Src/safety.c:33-48` - Safety_Check_Stall() 参考模式
  - `Core/Src/gpio.c:72-76` - 防撞杆GPIO已配置为PULLDOWN输入

  **Acceptance Criteria**:
  - [ ] 防撞杆检测函数已添加
  - [ ] 撞到上升防撞杆时pulse_count设为max_pulses
  - [ ] 撞到下降防撞杆时pulse_count设为0
  - [ ] 在Safety_Running_Update()中调用

  **Commit**: YES
  - Message: `feat(safety): add collision rod detection with auto height set`
  - Files: `APP/Inc/safety.h`, `APP/Src/safety.c`

- [ ] 3. 实现双键强制下降功能

  **What to do**:
  - 修改 `APP/Src/key.c`：在 `Key_Jog_Start_Check()` 末尾添加：
    ```c
    /* 双键强制下降：UP+DOWN同时按 → 绕过at_lower_limit，但防撞杆仍有效 */
    if (g_command.button_up && g_command.button_down
        && g_command.direction == DIR_STOP) {

        // 防撞杆最高优先级：下降防撞杆触发时不允许下降
        if (g_safety.left_down_collision || g_safety.right_down_collision) {
            #if CTRL_DEBUG == 1
            elog_w("CTRL", "Force DOWN blocked by collision rod");
            #endif
            return;
        }

        // 绕过at_lower_limit，强制下降
        g_safety.at_lower_limit = 0;
        Motor_Start_All(DIR_DOWN);
        g_command.direction = DIR_DOWN;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Force DOWN (dual key)");
        #endif
    }
    ```
  - 注意：需要在key.c中添加 `#include "safety.h"` （已有）

  **Must NOT do**:
  - 不能绕过防撞杆检查
  - 不能修改已有的单键逻辑

  **References**:
  - `APP/Src/key.c:94-132` - Key_Jog_Start_Check() 现有逻辑
  - `APP/Src/key.c:136-146` - Key_Jog_Conflict_Check() 双键检测参考

  **Acceptance Criteria**:
  - [ ] 双键同时按下时启动强制下降
  - [ ] 绕过at_lower_limit检查
  - [ ] 防撞杆触发时仍然阻止下降

  **Commit**: YES
  - Message: `feat(key): add dual-key force descent bypassing lower limit`
  - Files: `APP/Src/key.c`

- [ ] 4. 修改保存策略（停止时立即保存）

  **What to do**:
  - 修改 `APP/Src/app_w25qxx.c`：重写 `App_W25Qxx_Height_Save_If_Needed()`:
    ```c
    void App_W25Qxx_Height_Save_If_Needed(void)
    {
        /* 停止时立即保存：只有方向=停止时才保存 */
        if (g_command.direction != DIR_STOP) return;

        /* 防抖：停止后延迟200ms再保存，确保脉冲计数稳定 */
        static uint32_t stop_tick = 0;
        static uint8_t  pending = 0;

        if (!pending) {
            stop_tick = HAL_GetTick();
            pending = 1;
            return;
        }

        if (HAL_GetTick() - stop_tick < 200) return;

        /* 保存高度 */
        App_W25Qxx_Height_Save();
        pending = 0;

        #if W25Q_DEBUG == 1
        elog_i("W25Q", "Height saved on stop");
        #endif
    }
    ```
  - 删除旧的定时保存逻辑（基于tick差值的5秒保存）

  **Must NOT do**:
  - 不在运动中保存
  - 不修改App_W25Qxx_Height_Save()本身

  **References**:
  - `APP/Src/app_w25qxx.c:260-282` - 现有Height_Save_If_Needed()
  - `APP/Src/app_w25qxx.c:221-245` - App_W25Qxx_Height_Save() 实现

  **Acceptance Criteria**:
  - [ ] 只在direction==DIR_STOP时保存
  - [ ] 停止后200ms防抖再保存
  - [ ] 删除旧的定时保存逻辑

  **Commit**: YES
  - Message: `feat(flash): save height immediately on stop with debounce`
  - Files: `APP/Src/app_w25qxx.c`

- [ ] 5. 启用main.c中W25Q初始化和高度加载

  **What to do**:
  - 修改 `Core/Src/main.c`:
    - 第31行：取消注释 `#include "app_w25qxx.h"`
    - 第116行：取消注释 `App_W25Qxx_System_Init();`
    - 第117行：取消注释 `App_W25Qxx_Height_Load();`

  **References**:
  - `Core/Src/main.c:31` - include被注释
  - `Core/Src/main.c:116-117` - 初始化调用被注释

  **Acceptance Criteria**:
  - [ ] `#include "app_w25qxx.h"` 已取消注释
  - [ ] `App_W25Qxx_System_Init()` 已取消注释
  - [ ] `App_W25Qxx_Height_Load()` 已取消注释

  **Commit**: YES
  - Message: `feat(main): enable W25Q flash init and height load`
  - Files: `Core/Src/main.c`

- [ ] 6. 启用motor.c中高度保存调用

  **What to do**:
  - 修改 `Driver/Src/dri_motor.c`：取消注释第46行 `App_W25Qxx_Height_Save_If_Needed()`

  **References**:
  - `Driver/Src/dri_motor.c:46` - 保存调用被注释

  **Acceptance Criteria**:
  - [ ] `App_W25Qxx_Height_Save_If_Needed()` 已取消注释

  **Commit**: YES
  - Message: `feat(motor): enable height save on stop`
  - Files: `Driver/Src/dri_motor.c`

- [ ] 7. 更新调试开关

  **What to do**:
  - 修改 `Core/Inc/main.h`:
    - `W25Q_DEBUG` 从 0 改为 1（启用Flash调试日志）
    - `COLLISION_ENABLE` 从 0 改为 1（启用防撞杆）

  **References**:
  - `Core/Inc/main.h:121` - W25Q_DEBUG=0
  - `Core/Inc/main.h:126` - COLLISION_ENABLE=0

  **Acceptance Criteria**:
  - [ ] `W25Q_DEBUG=1`
  - [ ] `COLLISION_ENABLE=1`

  **Commit**: YES
  - Message: `config(debug): enable W25Q debug and collision detection`
  - Files: `Core/Inc/main.h`

- [ ] 8. 编译验证

  **What to do**:
  - 编译项目
  - 确认无错误、无警告（或只允许已知警告）

  **Acceptance Criteria**:
  - [ ] 编译通过，无新增错误

---

## Final Verification Wave

- [ ] F1. 检查所有修改文件的完整性
- [ ] F2. 确认防撞杆逻辑覆盖4个引脚
- [ ] F3. 确认双键强制下降绕过下限位但不绕过防撞杆
- [ ] F4. 确认CS引脚改为PB6

---

## Commit Strategy

分7次提交，每次提交一个功能模块：
1. `fix(spi): correct W25Q CS pin to PB6`
2. `feat(safety): add collision rod detection with auto height set`
3. `feat(key): add dual-key force descent bypassing lower limit`
4. `feat(flash): save height immediately on stop with debounce`
5. `feat(main): enable W25Q flash init and height load`
6. `feat(motor): enable height save on stop`
7. `config(debug): enable W25Q debug and collision detection`

---

## Success Criteria

### Final Checklist
- [ ] CS引脚=PB6
- [ ] 4个防撞杆检测正常工作
- [ ] 双键强制下降功能正常
- [ ] 停止时自动保存高度
- [ ] 编译通过
