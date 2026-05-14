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

/* ==================== 初始化 ==================== */

void Key_Init(void)
{
    Buzzer_Off();
}

/* ==================== 按键扫描（Key_Task 20ms 调用） ==================== */

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
                    elog_i("KEY", "Key %d press", i);
                    #endif
                } else {
                    g_key[i].state = KEY_STATE_IDLE;
                }
                break;

            case KEY_STATE_CONFIRMED:
                if (raw == KEY_RELEASE) {
                    g_key[i].f_hold = 0;
                    g_key[i].state = KEY_STATE_IDLE;
                }
                break;
        }
    }

    g_command.button_up      = g_key[0].f_hold;
    g_command.button_down    = g_key[1].f_hold;
    g_command.button_stop    = g_key[2].f_push;
    g_command.button_confirm = g_key[3].f_push;

    if (g_key[2].f_push) g_key[2].f_push = 0;
    if (g_key[3].f_push) g_key[3].f_push = 0;
}

/* ==================== 蜂鸣器 ==================== */
static uint32_t buzzer_off_tick = 0;

void Buzzer_On(void)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
}

void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
}

void Buzzer_Beep(uint32_t duration_ms)
{
    uint32_t now = HAL_GetTick();
    if (buzzer_off_tick && now < buzzer_off_tick) {
        Buzzer_On();
        buzzer_off_tick = now + duration_ms;
        return;
    }
    Buzzer_On();
    buzzer_off_tick = now + duration_ms;
    #if KEY_DEBUG == 1
    elog_a("KEY", "Buzz: %lums", duration_ms);
    #endif
}

void Buzzer_Poll(void)
{
    if (buzzer_off_tick && HAL_GetTick() >= buzzer_off_tick) {
        Buzzer_Off();
        buzzer_off_tick = 0;
    }
}

/* ==================== 点动控制 ==================== */

static uint8_t s_jog_limit_printed = 0;

void Key_Jog_Release_Check(void)
{
    if (!g_command.button_up && g_command.direction == DIR_UP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        s_jog_limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: STOP");
        #endif
    }
    if (!g_command.button_down && g_command.direction == DIR_DOWN) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        s_jog_limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: STOP");
        #endif
    }
}

void Key_Jog_Start_Check(void)
{
    /* ---- 上升 ---- */
    if (g_command.button_up && !g_command.button_down
        && g_command.direction == DIR_STOP) {

        if (g_safety.at_upper_limit) {
            #if CTRL_DEBUG == 1
            if (!s_jog_limit_printed) {
                elog_i("CTRL", "At top (%dmm)", HEIGHT_MM(g_config.max_pulses));
                s_jog_limit_printed = 1;
            }
            #endif
            return;
        }

        Motor_Start_All(DIR_UP);
        g_command.direction = DIR_UP;
        g_safety.at_lower_limit = 0;
        s_jog_limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: UP");
        #endif
        return;
    }

    /* ---- 下降 ---- */
    if (g_command.button_down && !g_command.button_up
        && g_command.direction == DIR_STOP) {

        if (g_safety.at_lower_limit) {
            #if CTRL_DEBUG == 1
            if (!s_jog_limit_printed) {
                elog_i("CTRL", "At bottom");
                s_jog_limit_printed = 1;
            }
            #endif
            return;
        }

        if (g_safety.secondary_descent_triggered
            && !g_safety.secondary_descent_confirmed) {
            Buzzer_Beep(2000);
            return;
        }

        Motor_Start_All(DIR_DOWN);
        g_command.direction = DIR_DOWN;
        g_safety.at_upper_limit = 0;
        Buzzer_Beep(500);
        s_jog_limit_printed = 0;
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
        s_jog_limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: STOP (conflict)");
        #endif
    }
}
