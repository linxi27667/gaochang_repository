#include "key.h"
#include "motor.h"
#include "safety.h"

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

/* 按键扫描状态机 — 每20ms调用一次
 * 两个按键(上升/下降)独立处理：空闲 → 消抖 → 确认按下 → 空闲
 * f_push: 按下边沿标志，确认按下瞬间置1
 * f_hold: 保持标志，按下期间持续为1，松开清零 */
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
}

/* 松手停止：松开按键 → 停止 */
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
}

/* 点动启动：按住才动，松手就停 */
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

    /* 双键强制下降：绕过下限位，防撞杆仍有效 */
    if (g_command.button_up && g_command.button_down
        && g_command.direction == DIR_STOP) {

        if (HAL_GPIO_ReadPin(Left_Down_Safety_GPIO_Port, Left_Down_Safety_Pin) ||
            HAL_GPIO_ReadPin(Right_Down_Safety_GPIO_Port, Right_Down_Safety_Pin)) {
            #if CTRL_DEBUG == 1
            elog_w("CTRL", "Force DOWN blocked by collision rod");
            #endif
            return;
        }

        g_safety.at_lower_limit = 0;
        Motor_Start_All(DIR_DOWN);
        g_command.direction = DIR_DOWN;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Force DOWN (dual key)");
        #endif
    }
}

/* 双键冲突：运动中同时按下 → 停止 */
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
