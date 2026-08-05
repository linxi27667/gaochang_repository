#include "balance.h"
#include "motor.h"
#include "encoder.h"
#include "safety.h"
#include "key.h"
#include "app_w25qxx.h"

#if BALANCE_DEBUG == 1
#include "elog.h"
#endif

void Balance_Run(void)
{
    if (!g_config.dual_column_mode) return;

    int32_t count_0 = Encoder_Get_Count(0);
    int32_t count_1 = Encoder_Get_Count(1);

    int32_t  difference;
    uint8_t  faster_column;

    if (g_command.direction == DIR_UP) {
        if (count_0 > count_1) {
            difference = count_0 - count_1;
            faster_column = 0;
        } else {
            difference = count_1 - count_0;
            faster_column = 1;
        }
    } else {
        if (count_0 < count_1) {
            difference = count_1 - count_0;
            faster_column = 0;
        } else {
            difference = count_0 - count_1;
            faster_column = 1;
        }
    }

    uint16_t tolerance_pause = (g_command.direction == DIR_UP)
                                ? g_config.tolerance_up
                                : g_config.tolerance_down;
    uint16_t tolerance_resume = 1;  /* 恢复阈值，与暂停阈值形成滞回防抖 */

    if (difference > tolerance_pause) {
        if (g_column[faster_column].motor_state == MOTOR_RUNNING) {
            Motor_Pause(faster_column);
            g_column[faster_column].motor_state = MOTOR_WAITING_BALANCE;
            g_column[faster_column].wait_start_tick = HAL_GetTick();
            #if BALANCE_DEBUG == 1
            elog_i("BAL", "[BAL] PAUSE col=%d diff=%ld pause_tol=%d dir=%s",
                   faster_column, difference, tolerance_pause,
                   g_command.direction == DIR_UP ? "UP" : "DOWN");
            #endif
        }
    } else {
        for (int i = 0; i < 2; i++) {
            if (g_column[i].motor_state == MOTOR_WAITING_BALANCE
                && difference <= tolerance_resume) {
                Motor_Start(i, g_command.direction);
                #if BALANCE_DEBUG == 1
                elog_i("BAL", "[BAL] RESUME col=%d diff=%ld resume_tol=%d", i, difference, tolerance_resume);
                #endif
            }
        }
    }

    uint32_t now = HAL_GetTick();
    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state == MOTOR_WAITING_BALANCE) {
            if (now - g_column[i].wait_start_tick > g_config.balance_wait_max_ms) {
                Motor_Stop_All();
                g_safety.alarm = ALARM_BALANCE_TIMEOUT;
                #if BALANCE_DEBUG == 1
                elog_e("BAL", "[BAL] Balance timeout! col=%d", i);
                #endif
            }
        }
    }
}
