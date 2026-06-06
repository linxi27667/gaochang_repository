#include "key.h"
#include "motor.h"
#include "safety.h"
#include "app_w25qxx.h"

#if KEY_DEBUG == 1 || CTRL_DEBUG == 1
#include "elog.h"
#endif

key_t g_key[MAX_KEY_NUM] = {
    [0] = { .port = KEY_UP_PORT,   .pin = KEY_UP_PIN   },
    [1] = { .port = KEY_DOWN_PORT, .pin = KEY_DOWN_PIN },
};

key_command_t g_command = {0};

void Key_Init(void)
{
}

void Key_Scan(void)
{
    for (int i = 0; i < MAX_KEY_NUM; i++) {
        uint8_t raw = HAL_GPIO_ReadPin(g_key[i].port, g_key[i].pin);

        switch (g_key[i].state) {
            case KEY_STATE_IDLE:
                if (raw == KEY_PRESS)
                    g_key[i].state = KEY_STATE_DEBOUNCE;
                break;

            case KEY_STATE_DEBOUNCE:
                if (raw == KEY_PRESS) {
                    g_key[i].f_push = 1;
                    g_key[i].f_hold = 1;
                    g_key[i].state = KEY_STATE_CONFIRMED;
                    #if KEY_DEBUG == 1
                    elog_i("KEY", "[KEY] Key%d PRESS", i);
                    #endif
                } else {
                    g_key[i].state = KEY_STATE_IDLE;
                }
                break;

            case KEY_STATE_CONFIRMED:
                if (raw == KEY_RELEASE) {
                    g_key[i].f_hold = 0;
                    g_key[i].state = KEY_STATE_IDLE;
                    #if KEY_DEBUG == 1
                    elog_i("KEY", "[KEY] Key%d RELEASE", i);
                    #endif
                }
                break;
        }
    }

    g_command.button_up   = g_key[0].f_hold;
    g_command.button_down = g_key[1].f_hold;
}

void Key_Jog_Release_Check(void)
{
    if (!g_command.button_up && g_command.direction == DIR_UP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "[CTRL] Stopped: up released");
        #endif
    }
    if (!g_command.button_down && g_command.direction == DIR_DOWN) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "[CTRL] Stopped: down released");
        #endif
    }
}

void Key_Jog_Start_Check(void)
{
    if (g_command.button_up && g_command.button_down) return;
    if (g_command.direction != DIR_STOP) return;

    if (g_command.button_up) {
        if (!Safety_Can_Move(DIR_UP)) {
            Safety_Report_Blocked_Move(DIR_UP);
            return;
        }

        Motor_Start_All(DIR_UP);
        if (g_column[0].motor_state == MOTOR_RUNNING || g_column[1].motor_state == MOTOR_RUNNING) {
            g_command.direction = DIR_UP;
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "[CTRL] Direction UP left=%ldmm right=%ldmm",
                   HEIGHT_MM(g_column[0].pulse_count), HEIGHT_MM(g_column[1].pulse_count));
            #endif
        }
        return;
    }

    if (g_command.button_down) {
        if (!Safety_Can_Move(DIR_DOWN)) {
            Safety_Report_Blocked_Move(DIR_DOWN);
            return;
        }

        Motor_Start_All(DIR_DOWN);
        if (g_column[0].motor_state == MOTOR_RUNNING || g_column[1].motor_state == MOTOR_RUNNING) {
            g_command.direction = DIR_DOWN;
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "[CTRL] Direction DOWN left=%ldmm right=%ldmm",
                   HEIGHT_MM(g_column[0].pulse_count), HEIGHT_MM(g_column[1].pulse_count));
            #endif
        }
    }
}

void Key_Jog_Conflict_Check(void)
{
    static uint8_t conflict_active = 0;

    if (g_command.button_up && g_command.button_down) {
        if (g_command.direction != DIR_STOP) {
            Motor_Stop_All_Immediate();
            g_command.direction = DIR_STOP;
        }
        if (!conflict_active) {
            conflict_active = 1;
            #if CTRL_DEBUG == 1
            elog_w("CTRL", "[CTRL] Dual key conflict: motion inhibited");
            #endif
        }
        return;
    }

    conflict_active = 0;
}
