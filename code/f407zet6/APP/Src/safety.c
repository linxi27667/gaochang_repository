#include "safety.h"
#include "encoder.h"
#include "key.h"
#include "app_w25qxx.h"
#include "app_buzzer.h"
#include "cmsis_os.h"
#include "balance.h"

#if SAFETY_DEBUG == 1 || CTRL_DEBUG == 1
#include "elog.h"
#endif

safety_state_t g_safety = { .alarm = ALARM_NONE };
static direction_t s_last_reported_block = DIR_STOP;

static uint8_t any_up_collision(void)
{
    return g_safety.left_up_collision || g_safety.right_up_collision;
}

static uint8_t any_down_collision(void)
{
    return g_safety.left_down_collision || g_safety.right_down_collision;
}

static const char *direction_name(direction_t direction)
{
    if (direction == DIR_UP) return "UP";
    if (direction == DIR_DOWN) return "DOWN";
    return "STOP";
}

static void reset_height_to_zero_on_lower_limit(void)
{
    int32_t old_left = Encoder_Get_Count(0);
    int32_t old_right = Encoder_Get_Count(1);
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_column[0].pulse_count = 0;
    g_column[1].pulse_count = 0;
    __set_PRIMASK(primask);

    #if SAFETY_DEBUG == 1
    elog_w("SAFETY", "[SAFETY] Lower limit reached, height reset old=%ld/%ldmm new=0/0mm",
           HEIGHT_MM(old_left), HEIGHT_MM(old_right));
    #endif
}

void Safety_Init(void)
{
    g_safety.secondary_descent_triggered   = 0;
    g_safety.secondary_descent_confirmed   = 0;
    g_safety.at_lower_limit                = 0;
    g_safety.at_upper_limit                = 0;
    g_safety.stall_suspected               = 0;
    g_safety.alarm                         = ALARM_NONE;
    g_safety.left_up_collision             = 0;
    g_safety.right_up_collision            = 0;
    g_safety.left_down_collision           = 0;
    g_safety.right_down_collision          = 0;
    g_safety.collision_warn_up             = 0;
    g_safety.collision_warn_down           = 0;
}

uint8_t Safety_Can_Move(direction_t direction)
{
    if (direction == DIR_UP && any_up_collision()) return 0;
    if (direction == DIR_DOWN && any_down_collision()) return 0;

    if (g_safety.alarm != ALARM_NONE && g_safety.alarm != ALARM_COLLISION) return 0;
    if (g_safety.stall_suspected) return 0;

    return 1;
}

void Safety_Report_Blocked_Move(direction_t direction)
{
    uint8_t blocked_by_collision =
        (direction == DIR_UP && any_up_collision()) ||
        (direction == DIR_DOWN && any_down_collision());

    if (!blocked_by_collision) return;

    Motor_Stop_All_Immediate();
    g_command.direction = DIR_STOP;
    g_safety.alarm = ALARM_COLLISION;
    App_Buzzer_Alarm(500);

    if (s_last_reported_block != direction) {
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "[SAFETY] Collision blocks %s: up=%d%d down=%d%d left=%ldmm right=%ldmm",
               direction_name(direction),
               g_safety.left_up_collision, g_safety.right_up_collision,
               g_safety.left_down_collision, g_safety.right_down_collision,
               HEIGHT_MM(g_column[0].pulse_count), HEIGHT_MM(g_column[1].pulse_count));
        #endif
        s_last_reported_block = direction;
    }
}

void Safety_Check_Collision(void)
{
    static uint8_t last_up_raw = 0;
    static uint8_t last_down_raw = 0;

    g_safety.left_up_collision    = HAL_GPIO_ReadPin(Left_Up_Safety_GPIO_Port, Left_Up_Safety_Pin);
    g_safety.right_up_collision   = HAL_GPIO_ReadPin(Right_Up_Safety_GPIO_Port, Right_Up_Safety_Pin);
    g_safety.left_down_collision  = HAL_GPIO_ReadPin(Left_Down_Safety_GPIO_Port, Left_Down_Safety_Pin);
    g_safety.right_down_collision = HAL_GPIO_ReadPin(Right_Down_Safety_GPIO_Port, Right_Down_Safety_Pin);

    uint8_t up_raw = any_up_collision();
    uint8_t down_raw = any_down_collision();

    g_safety.at_upper_limit = up_raw;
    g_safety.at_lower_limit = down_raw;

    if (up_raw && !last_up_raw) {
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "[SAFETY] Upper collision input HIGH left=%d right=%d",
               g_safety.left_up_collision, g_safety.right_up_collision);
        #endif
    }
    if (down_raw && !last_down_raw) {
        reset_height_to_zero_on_lower_limit();
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "[SAFETY] Lower collision input HIGH left=%d right=%d",
               g_safety.left_down_collision, g_safety.right_down_collision);
        #endif
    }

    g_safety.collision_warn_up = up_raw;
    g_safety.collision_warn_down = down_raw;

    if ((up_raw && g_command.direction == DIR_UP) ||
        (down_raw && g_command.direction == DIR_DOWN)) {
        Safety_Report_Blocked_Move(g_command.direction);
    }

    if (g_safety.alarm == ALARM_COLLISION && !up_raw && !down_raw) {
        g_safety.alarm = ALARM_NONE;
        s_last_reported_block = DIR_STOP;
        App_Buzzer_Off();
        #if SAFETY_DEBUG == 1
        elog_a("SAFETY", "[SAFETY] Collision alarm cleared by pins LOW left=%ldmm right=%ldmm",
               HEIGHT_MM(g_column[0].pulse_count), HEIGHT_MM(g_column[1].pulse_count));
        #endif
    } else if (!up_raw && !down_raw) {
        s_last_reported_block = DIR_STOP;
    }

    last_up_raw = up_raw;
    last_down_raw = down_raw;
}

void Safety_EXTI_Handler(uint16_t gpio_pin)
{
    (void)gpio_pin;
}

void Safety_Check_Upper_Limit(void)
{
}

void Safety_Check_Lower_Limit(void)
{
}

void Safety_Check_Stall(void)
{
    uint32_t now = HAL_GetTick();

    if (g_safety.alarm != ALARM_NONE) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (now - g_safety.last_pulse_tick[i] > g_config.stall_timeout_ms) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            g_safety.alarm = ALARM_STALL;
            g_safety.stall_suspected = 1;
            App_Buzzer_Alarm(1000);
            #if SAFETY_DEBUG == 1
            elog_e("SAFETY", "[SAFETY] Stall col=%d left=%ldmm right=%ldmm",
                   i, HEIGHT_MM(g_column[0].pulse_count), HEIGHT_MM(g_column[1].pulse_count));
            #endif
            return;
        }
    }
}

#if SECONDARY_DESCENT_ENABLE == 1
void Safety_Check_Secondary_Descent(void)
{
    if (g_command.direction != DIR_DOWN) return;
    if (g_safety.secondary_descent_triggered) return;

    int32_t height_0 = Encoder_Get_Count(0);
    int32_t height_1 = Encoder_Get_Count(1);
    uint16_t pulses = g_config.secondary_descent_pulses;

    if (height_0 <= (int32_t)pulses || height_1 <= (int32_t)pulses) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        g_safety.secondary_descent_triggered = 1;
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "[SAFETY] Near bottom, confirm to continue");
        #endif
    }
}
#else
void Safety_Check_Secondary_Descent(void) {}
#endif

void Safety_Alarm_Reset(void)
{
    if (g_safety.alarm == ALARM_NONE && !g_safety.stall_suspected) return;

    g_safety.alarm = ALARM_NONE;
    g_safety.stall_suspected = 0;
    g_safety.secondary_descent_triggered = 0;
    g_safety.secondary_descent_confirmed = 0;
    g_command.direction = DIR_STOP;
    App_Buzzer_Off();
    #if SAFETY_DEBUG == 1
    elog_i("SAFETY", "[SAFETY] Alarm reset");
    #endif
}

uint8_t Safety_Alarm_Handle(void)
{
    if (g_safety.alarm == ALARM_NONE)
        return 0;

    if (g_safety.alarm == ALARM_COLLISION)
        return 0;

    return 1;
}

void Safety_Running_Update(void)
{
    Safety_Check_Collision();
    if (g_command.direction == DIR_STOP) return;

    Balance_Run();
    Safety_Check_Secondary_Descent();
}
