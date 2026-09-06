#include "lift_core.h"
#include "app_io_map.h"
#include "app_w25qxx.h"
#include "app_op_log.h"
#include "app_product.h"
#include "stm32f1xx_hal.h"
#include "main.h"

#if DOUBLE_POST_DEBUG == 1
#include "elog.h"
#endif

/* ============ 涓ゆ煴鍐呴儴瀛愮姸鎬?============ */
typedef enum {
    DP_IDLE = 0,
    DP_UP_RUNNING,       /* 涓婂崌锛氱數鏈?1 */
    DP_DOWN_PHASE_1,     /* 涓嬮檷闃舵1锛氱數鏈?1锛岀瓑寰呯數纾侀搧寤舵椂 (200ms) */
    DP_DOWN_PHASE_2,     /* 涓嬮檷闃舵2锛氱數鏈?1+鐢电閾?1锛岀瓑寰呯數鏈轰繚鎸?(鍒?000ms) */
    DP_DOWN_FORCED_UNLOCK, /* 涓嬮檷寮哄埗瑙ｉ攣锛氱數鏈?0+鐢电閾?1锛岀瓑 2s 寮€涓嬮檷闃€ */
    DP_DOWN_DROPPING,    /* 涓嬮檷闃舵3锛氱數鏈?0+鐢电閾?1+涓嬮檷闃€=1 */
    DP_LOCKED            /* 閿佸畾淇濇寔锛氫笅闄嶉榾=1 */
} dp_sub_state_t;

/* ============ 涓嬮檷鏃跺簭鍙傛暟锛堢敤鎴疯鏍硷紝鍥哄畾鍊硷級 ============ */
#define DP_ELECTROMAGNET_DELAY_MS  200U   /* 鐢垫満鎺ラ€?鈫?鐢电閾佹帴閫氬欢鏃?0.2s */
#define DP_MOTOR_HOLD_TOTAL_MS     2000U  /* 鐢垫満鎺ラ€?鈫?鐢垫満鏂紑鎬绘椂闂?2s */

/* ============ 妯″潡鍐呴儴鐘舵€?============ */
static dp_sub_state_t s_dp_state = DP_IDLE;
static uint32_t       s_action_start_tick = 0;

/* 鎸夐挳杈规部妫€娴嬶紙涓婁竴鍛ㄦ湡鐘舵€侊級 */
static uint8_t s_prev_up   = 0;
static uint8_t s_prev_down = 0;
static uint8_t s_prev_lock = 0;

/* ============ 杈呭姪瀹忥細涓ゆ煴杈撳嚭璇箟閲嶆槧灏?============ */
/* 涓ゆ煴"鐢电閾?澶嶇敤 IO_OUT_ELECTROMAGNET 妲戒綅锛堝ぇ鍓富鏈烘皵闃€锛?*/
#define DP_WRITE_MOTOR(v)         App_IO_Write(IO_OUT_MOTOR, (v))
#define DP_WRITE_ELECTROMAGNET(v) App_IO_Write(IO_OUT_ELECTROMAGNET, (v))
#define DP_WRITE_DROP_VALVE(v)    App_IO_Write(IO_OUT_DROP_VALVE, (v))

/* 鍋滄鎵€鏈夎緭鍑哄苟鍥炲埌 IDLE */
static void dp_stop_all_to_idle(void)
{
    App_IO_All_Off();
    s_dp_state = DP_IDLE;
    g_lift_state = LIFT_STATE_IDLE;
}

/* ============ 鍒濆鍖?============ */
static void double_post_init(void)
{
    s_dp_state = DP_IDLE;
    s_action_start_tick = 0;
    s_prev_up = s_prev_down = s_prev_lock = 0;
    App_IO_All_Off();

#if DOUBLE_POST_DEBUG == 1
    elog_i("DP", "[DP] Init done (double_post)");
#endif
}

/* ============ 涓婂崌娴佺▼ ============ */
static void dp_start_up(void)
{
    /* Rising is a motor-only operation. Clear any valve left by a prior
     * descent/lock path before energizing the motor. */
    DP_WRITE_ELECTROMAGNET(0);
    DP_WRITE_DROP_VALVE(0);
    DP_WRITE_MOTOR(1);
    s_action_start_tick = HAL_GetTick();
    s_dp_state = DP_UP_RUNNING;
    g_lift_state = LIFT_STATE_RISING;

    App_OpLog_Record(OP_UP_START, OP_RESULT_OK, 0, NULL, 0);

#if DOUBLE_POST_DEBUG == 1
    elog_i("DP", "[DP] UP start");
#endif
}

static void dp_poll_up(void)
{
    uint32_t now = HAL_GetTick();

    /* Keep the rising output set fail-safe even if another stop path left a
     * stale valve request in the output cache. */
    DP_WRITE_ELECTROMAGNET(0);
    DP_WRITE_DROP_VALVE(0);

    /* 閫€鍑烘潯浠讹細涓婇檺浣嶈Е鍙?鎴?鎸夐挳閲婃斁 鈫?鐢垫満鏂紑 */
    uint8_t upper_limit = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t up_pressed  = App_IO_Read(IO_IN_UP_BUTTON);

    if (upper_limit) {
        /* 涓婇檺浣嶈Е鍙戯細鍏ㄥ仠 */
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        dp_stop_all_to_idle();
        App_OpLog_Record(OP_UP_STOP_LIMIT, OP_RESULT_OK, dur, NULL, 0);
#if DOUBLE_POST_DEBUG == 1
        elog_i("DP", "[DP] UP stop by limit, dur=%u", dur);
#endif
    } else if (!up_pressed) {
        /* 鎸夐挳閲婃斁锛氬叏鍋?*/
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        dp_stop_all_to_idle();
        App_OpLog_Record(OP_UP_STOP_RELEASE, OP_RESULT_OK, dur, NULL, 0);
#if DOUBLE_POST_DEBUG == 1
        elog_i("DP", "[DP] UP stop by release, dur=%u", dur);
#endif
    }
}

/* ============ 涓嬮檷娴佺▼ ============ */
static void dp_start_down(void)
{
    DP_WRITE_MOTOR(1);
    s_action_start_tick = HAL_GetTick();
    s_dp_state = DP_DOWN_PHASE_1;
    g_lift_state = LIFT_STATE_DROPPING;

    App_OpLog_Record(OP_DOWN_START, OP_RESULT_OK, 0, NULL, 0);
    App_W25Qxx_Stats_Inc_Down(LIFT_ROLE_MAIN);

#if DOUBLE_POST_DEBUG == 1
    elog_i("DP", "[DP] DOWN start");
#endif
}

static void dp_poll_down(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - s_action_start_tick;

    /* 闃舵1鈫?锛氱數鏈烘帴閫?200ms 鍚庣數纾侀搧鎺ラ€?*/
    if (s_dp_state == DP_DOWN_PHASE_1) {
        if (elapsed >= DP_ELECTROMAGNET_DELAY_MS) {
            DP_WRITE_ELECTROMAGNET(1);
            s_dp_state = DP_DOWN_PHASE_2;
#if DOUBLE_POST_DEBUG == 1
            elog_i("DP", "[DP] DOWN electromagnet ON (t=%lu)", (unsigned long)elapsed);
#endif
        }
    }
    /* 闃舵2鈫?锛氱數鏈烘帴閫?2000ms 鍚庣數鏈烘柇寮€ + 涓嬮檷闃€鎵撳紑 */
    else if ((s_dp_state == DP_DOWN_PHASE_2) ||
             (s_dp_state == DP_DOWN_FORCED_UNLOCK)) {
        if (elapsed >= DP_MOTOR_HOLD_TOTAL_MS) {
            DP_WRITE_MOTOR(0);
            DP_WRITE_DROP_VALVE(1);
            s_dp_state = DP_DOWN_DROPPING;
#if DOUBLE_POST_DEBUG == 1
            elog_i("DP", "[DP] DOWN motor OFF, drop valve ON (t=%lu)", (unsigned long)elapsed);
#endif
        }
    }

    /* 寮哄埗涓嬮檷鏉′欢锛氫笂闄愪綅瑙﹀彂 鎴?閿佸畾鎸夐挳鎸変笅
     * 鐢垫満鏂紑锛岀數纾侀搧+涓嬮檷闃€绔嬪嵆鎵撳紑
     */
    uint8_t upper_limit  = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t down_pressed = App_IO_Read(IO_IN_DOWN_BUTTON);

    if ((s_dp_state != DP_DOWN_DROPPING) &&
        (s_dp_state != DP_DOWN_FORCED_UNLOCK)) {
        if (upper_limit || lock_pressed) {
            DP_WRITE_MOTOR(0);
            DP_WRITE_ELECTROMAGNET(1);
            s_dp_state = DP_DOWN_FORCED_UNLOCK;
            g_lift_state = LIFT_STATE_DROPPING;
            App_OpLog_Record(OP_DOWN_STOP_LIMIT, OP_RESULT_OK, (uint16_t)elapsed, NULL, 0);
#if DOUBLE_POST_DEBUG == 1
            elog_i("DP", "[DP] DOWN force unlock: limit=%u lock=%u", upper_limit, lock_pressed);
#endif
        }
    }

    /* 鎸夐挳閲婃斁锛氬叏鍋滐紝鍥?IDLE */
    if (!down_pressed) {
        uint16_t dur = (uint16_t)elapsed;
        dp_stop_all_to_idle();
        App_OpLog_Record(OP_DOWN_STOP_RELEASE, OP_RESULT_OK, dur, NULL, 0);
#if DOUBLE_POST_DEBUG == 1
        elog_i("DP", "[DP] DOWN stop by release, dur=%u", dur);
#endif
    }
}

/* ============ 閿佸畾娴佺▼ ============ */
static void dp_start_lock(void)
{
    DP_WRITE_DROP_VALVE(1);
    s_dp_state = DP_LOCKED;
    s_action_start_tick = HAL_GetTick();
    g_lift_state = LIFT_STATE_LOCKED;

    App_OpLog_Record(OP_LOCK_START, OP_RESULT_OK, 0, NULL, 0);
    App_W25Qxx_Stats_Inc_Lock();

#if DOUBLE_POST_DEBUG == 1
    elog_i("DP", "[DP] LOCK start");
#endif
}

static void dp_poll_lock(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);

    if (!lock_pressed) {
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        dp_stop_all_to_idle();
        App_OpLog_Record(OP_LOCK_STOP, OP_RESULT_OK, dur, NULL, 0);
#if DOUBLE_POST_DEBUG == 1
        elog_i("DP", "[DP] LOCK stop, dur=%u", dur);
#endif
    }
}

/* ============ 鎬ュ仠/鍏夌數/杩滅▼瑙ｉ櫎 鍥炶皟 ============ */
static void double_post_on_estop(void)
{
    App_IO_All_Off();
    s_dp_state = DP_IDLE;
}

/* 涓ゆ煴鏃犲厜鐢靛紑鍏筹紝鐣欑┖瀹炵幇锛圠iftCore 鍏夌數妫€娴嬪凡鎸?product_type 璺宠繃锛?*/
static void double_post_on_photoelectric_blocked(void)
{
    /* nothing to do */
}

static void double_post_on_clear_alarm(void)
{
    /* 鎶ヨ宸茶В闄わ紝鐘舵€佸凡鐢?LiftCore 閲嶇疆涓?IDLE */
    s_dp_state = DP_IDLE;
}

/* ============ 涓?poll 鍑芥暟 ============ */
static void double_post_poll(void)
{
    /* 1. 璇诲彇鎸夐挳褰撳墠鐘舵€?*/
    uint8_t up_pressed   = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t down_pressed = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);

    /* 2. 鏍规嵁褰撳墠瀛愮姸鎬佸垎鍙戝鐞?*/
    switch (s_dp_state) {
        case DP_IDLE:
            /* 妫€娴嬫寜閽寜涓嬭竟娌匡紙浠?IDLE 鍚姩鏂板姩浣滐級
             * 浼樺厛绾э細涓婂崌 > 涓嬮檷 > 閿佸畾锛堜笌澶у壀涓€鑷达級
             */
            if (up_pressed && !s_prev_up) {
                dp_start_up();
            } else if (down_pressed && !s_prev_down) {
                dp_start_down();
            } else if (lock_pressed && !s_prev_lock) {
                dp_start_lock();
            }
            break;

        case DP_UP_RUNNING:
            dp_poll_up();
            break;

        case DP_DOWN_PHASE_1:
        case DP_DOWN_PHASE_2:
        case DP_DOWN_FORCED_UNLOCK:
        case DP_DOWN_DROPPING:
            dp_poll_down();
            break;

        case DP_LOCKED:
            dp_poll_lock();
            break;

        default:
            s_dp_state = DP_IDLE;
            break;
    }

    /* 3. 鏇存柊鎸夐挳鍘嗗彶鐘舵€侊紙鐢ㄤ簬杈规部妫€娴嬶級 */
    s_prev_up   = up_pressed;
    s_prev_down = down_pressed;
    s_prev_lock = lock_pressed;
}

/* ============ ops 瀹炰緥 ============ */
const lift_ops_t double_post_ops = {
    .init                     = double_post_init,
    .on_up_pressed            = NULL,  /* 鍦?poll 涓鐞?*/
    .on_down_pressed          = NULL,
    .on_lock_pressed          = NULL,
    .on_refill_pressed        = NULL,  /* 涓ゆ煴鏃犺ˉ娌?*/
    .on_estop                 = double_post_on_estop,
    .on_photoelectric_blocked = double_post_on_photoelectric_blocked,
    .on_clear_alarm           = double_post_on_clear_alarm,
    .poll                     = double_post_poll,
};
