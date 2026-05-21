#include "safety.h"
#include "motor.h"
#include "encoder.h"
#include "key.h"
#include "app_w25qxx.h"

#if SAFETY_DEBUG == 1 || CTRL_DEBUG == 1
#include "elog.h"
#endif
#include "cmsis_os.h"
#include "balance.h"

safety_state_t g_safety = { .alarm = ALARM_NONE };

/* ==================== 初始化 ==================== */

void Safety_Init(void)
{
    g_safety.secondary_descent_triggered   = 0;
    g_safety.secondary_descent_confirmed   = 0;
    g_safety.at_lower_limit                = 0;
    g_safety.at_upper_limit                = 0;
    g_safety.stall_suspected               = 0;
    g_safety.alarm                         = ALARM_NONE;
    g_safety.left_up_collision    = 0;
    g_safety.right_up_collision   = 0;
    g_safety.left_down_collision  = 0;
    g_safety.right_down_collision = 0;
}

/* ==================== 防撞杆 ==================== */

/* 读取4个防撞杆GPIO，高电平=撞到
 * 撞到上升防撞杆 → 停止上升 + 设置最大高度
 * 撞到下降防撞杆 → 停止下降 + 设置最小高度 */
void Safety_Check_Collision(void)
{
    g_safety.left_up_collision   = HAL_GPIO_ReadPin(Left_Up_Safety_GPIO_Port, Left_Up_Safety_Pin);
    g_safety.right_up_collision  = HAL_GPIO_ReadPin(Right_Up_Safety_GPIO_Port, Right_Up_Safety_Pin);
    g_safety.left_down_collision = HAL_GPIO_ReadPin(Left_Down_Safety_GPIO_Port, Left_Down_Safety_Pin);
    g_safety.right_down_collision= HAL_GPIO_ReadPin(Right_Down_Safety_GPIO_Port, Right_Down_Safety_Pin);

    uint8_t any_up_collision   = g_safety.left_up_collision || g_safety.right_up_collision;
    uint8_t any_down_collision = g_safety.left_down_collision || g_safety.right_down_collision;

    if (any_up_collision && g_command.direction == DIR_UP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        g_safety.at_upper_limit = 1;
    }
    if (any_down_collision && g_command.direction == DIR_DOWN) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        g_safety.at_lower_limit = 1;
    }

    if (g_safety.left_up_collision)   g_column[0].pulse_count = (int32_t)g_config.max_pulses;
    if (g_safety.right_up_collision)  g_column[1].pulse_count = (int32_t)g_config.max_pulses;
    if (g_safety.left_down_collision) g_column[0].pulse_count = 0;
    if (g_safety.right_down_collision)g_column[1].pulse_count = 0;
}

/* EXTI预留，当前使用轮询方式 */
void Safety_EXTI_Handler(uint16_t gpio_pin)
{
    (void)gpio_pin;
}

/* ==================== 限位保护 ==================== */

/* 上限位：pulse_count >= max_pulses 时停止上升 */
void Safety_Check_Upper_Limit(void)
{
    if (g_command.direction != DIR_UP) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;
        if (g_column[i].pulse_count >= (int32_t)g_config.max_pulses) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            g_safety.at_upper_limit = 1;
            #if SAFETY_DEBUG == 1
            elog_w("SAFETY", "Top reached (%ldmm)", (int32_t)g_config.max_pulses * SCREW_LEAD_MM);
            #endif
            return;
        }
    }
}

/* 下限位：pulse_count <= 0 时停止下降 */
void Safety_Check_Lower_Limit(void)
{
    if (g_command.direction != DIR_DOWN) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;
        if (g_column[i].pulse_count <= 0) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            g_safety.at_lower_limit = 1;
            #if SAFETY_DEBUG == 1
            elog_w("SAFETY", "Bottom reached");
            #endif
            return;
        }
    }
}

/* ==================== 堵转检测 ==================== */

/* 超过stall_timeout_ms没有脉冲 → 判定为堵转 */
void Safety_Check_Stall(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (now - g_safety.last_pulse_tick[i] > g_config.stall_timeout_ms) {
            Motor_Stop_All();
            g_safety.alarm           = ALARM_STALL;
            g_safety.stall_suspected = 1;
            #if SAFETY_DEBUG == 1
            elog_e("SAFETY", "Stall! col=%d", i);
            #endif
        }
    }
}

/* ==================== 二次下降 ==================== */

#if SECONDARY_DESCENT_ENABLE == 1
/* 接近底部时暂停，等待确认后继续下降 */
void Safety_Check_Secondary_Descent(void)
{
    if (g_command.direction != DIR_DOWN) return;
    if (g_safety.secondary_descent_triggered) return;

    int32_t height_0 = Encoder_Get_Count(0);
    int32_t height_1 = Encoder_Get_Count(1);
    uint16_t pulses = g_config.secondary_descent_pulses;

    if (height_0 <= (int32_t)pulses || height_1 <= (int32_t)pulses) {
        Motor_Stop_All();
        g_safety.secondary_descent_triggered = 1;
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "Near bottom, confirm to continue");
        #endif
    }
}
#else
void Safety_Check_Secondary_Descent(void) {}
#endif

/* ==================== 告警处理 ==================== */

/* 清除告警状态，恢复到正常模式 */
void Safety_Alarm_Reset(void)
{
    if (g_safety.alarm == ALARM_NONE) return;

    g_safety.alarm                       = ALARM_NONE;
    g_safety.stall_suspected             = 0;
    g_safety.secondary_descent_triggered = 0;
    g_safety.secondary_descent_confirmed = 0;
    g_command.direction                  = DIR_STOP;
    #if SAFETY_DEBUG == 1
    elog_i("SAFETY", "Alarm cleared");
    #endif
}

/* 告警状态处理（每10ms调用）
 * 碰撞告警：按下降键退出
 * 其他告警：双键同按复位 */
uint8_t Safety_Alarm_Handle(void)
{
    if (g_safety.alarm == ALARM_NONE)
        return 0;

    if (g_safety.alarm == ALARM_COLLISION
        && g_command.button_down && !g_command.button_up) {
        Safety_Alarm_Reset();
        g_safety.at_upper_limit = 0;
        return 0;
    }

    if (g_command.button_up && g_command.button_down) {
        Safety_Alarm_Reset();
        return 0;
    }

    return 1;
}

/* ==================== 运行时更新 ==================== */

/* 每10ms调用一次，按优先级执行各项检查 */
void Safety_Running_Update(void)
{
    Safety_Check_Collision();
    if (g_command.direction == DIR_STOP) return;

    Balance_Run();
    Safety_Check_Upper_Limit();
    Safety_Check_Lower_Limit();
    Safety_Check_Secondary_Descent();
}
