#include "balance.h"
#include "motor.h"
#include "encoder.h"
#include "safety.h"
#include "key.h"
#include "app_w25qxx.h"

#if BALANCE_DEBUG == 1
#include "elog.h"
#endif

/* ==================== 双柱平衡算法 ==================== */

void Balance_Run(void)
{
    if (!g_config.dual_column_mode) return;

    int32_t count_0 = Encoder_Get_Count(0);
    int32_t count_1 = Encoder_Get_Count(1);

    int32_t  difference;
    uint8_t  faster_column;

    /* 判定"快柱"：
     *   上升：脉冲多 = 升得高 = 快 → 暂停让它等慢柱
     *   下降：脉冲少 = 降得多 = 快 → 暂停让它等慢柱 */
    if (g_command.direction == DIR_UP) {
        if (count_0 > count_1) {
            difference = count_0 - count_1;
            faster_column = 0;
        } else {
            difference = count_1 - count_0;
            faster_column = 1;
        }
    } else {   /* DIR_DOWN */
        if (count_0 < count_1) {
            difference = count_1 - count_0;
            faster_column = 0;    /* 0#脉冲少，降得快 */
        } else {
            difference = count_0 - count_1;
            faster_column = 1;    /* 1#脉冲少，降得快 */
        }
    }

    uint16_t tolerance = (g_command.direction == DIR_UP)
                         ? g_config.tolerance_up
                         : g_config.tolerance_down;

    if (difference > tolerance) {
        /* 快柱超出允差 → 暂停快柱 */
        if (g_column[faster_column].motor_state == MOTOR_RUNNING) {
            Motor_Pause(faster_column);   /* 只停单柱，保留REVERSE/BRAKE */
            g_column[faster_column].motor_state = MOTOR_WAITING_BALANCE;
            g_column[faster_column].wait_start_tick = HAL_GetTick();
            #if BALANCE_DEBUG == 1
            elog_i("BAL", "PAUSE col=%d diff=%d tol=%d", faster_column, difference, tolerance);
            #endif
        }
    } else {
        /* 差值回到允差内 → 恢复等待中的柱子 */
        for (int i = 0; i < 2; i++) {
            if (g_column[i].motor_state == MOTOR_WAITING_BALANCE) {
                Motor_Start(i, g_command.direction);
                #if BALANCE_DEBUG == 1
                elog_i("BAL", "RESUME col=%d diff=%d", i, difference);
                #endif
            }
        }
    }

    /* 超时检查 */
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state == MOTOR_WAITING_BALANCE) {
            if (now - g_column[i].wait_start_tick > g_config.balance_wait_max_ms) {
                Motor_Stop_All();
                g_safety.alarm = ALARM_BALANCE_TIMEOUT;
                #if BALANCE_DEBUG == 1
                elog_e("BAL", "Balance timeout! col=%d", i);
                #endif
            }
        }
    }
}
