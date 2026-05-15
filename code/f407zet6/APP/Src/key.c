#include "key.h"
#include "motor.h"
#include "safety.h"

#if KEY_DEBUG == 1 || CTRL_DEBUG == 1
#include "elog.h"
#endif

key_t g_key[MAX_KEY_NUM] = {
    [0] = { .port = KEY_UP_PORT,   .pin = KEY_UP_PIN   },
    [1] = { .port = KEY_DOWN_PORT, .pin = KEY_DOWN_PIN },
    [2] = { .port = KEY_STOP_PORT, .pin = KEY_STOP_PIN },
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
                    elog_i("KEY", "Key%d PRESS", i);
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
                    elog_i("KEY", "Key%d RELEASE", i);
                    #endif
                }
                break;
        }
    }

    g_command.button_up   = g_key[0].f_hold;
    g_command.button_down = g_key[1].f_hold;
    g_command.button_stop = g_key[2].f_push;

    if (g_key[2].f_push) g_key[2].f_push = 0;
}

void Key_Jog_Release_Check(void)
{
    if (!g_command.button_up && g_command.direction == DIR_UP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "STOP (up released)");
        #endif
    }
    if (!g_command.button_down && g_command.direction == DIR_DOWN) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "STOP (down released)");
        #endif
    }

    if (g_command.button_stop && g_command.direction != DIR_STOP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "STOP (stop key)");
        #endif
    }
}

void Key_Jog_Start_Check(void)
{
    if (g_command.button_up && !g_command.button_down
        && g_command.direction == DIR_STOP) {

        if (g_safety.at_upper_limit) {
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "At top, skip UP");
            #endif
            return;
        }

        Motor_Start_All(DIR_UP);
        g_command.direction = DIR_UP;
        g_safety.at_lower_limit = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: UP");
        #endif
        return;
    }

    if (g_command.button_down && !g_command.button_up
        && g_command.direction == DIR_STOP) {

        if (g_safety.at_lower_limit) {
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "At bottom, skip DOWN");
            #endif
            return;
        }

        Motor_Start_All(DIR_DOWN);
        g_command.direction = DIR_DOWN;
        g_safety.at_upper_limit = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: DOWN");
        #endif
    }
}

void Key_Jog_Conflict_Check(void)
{
    if (g_command.button_up && g_command.button_down
        && g_command.direction != DIR_STOP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "STOP (conflict)");
        #endif
    }
}
