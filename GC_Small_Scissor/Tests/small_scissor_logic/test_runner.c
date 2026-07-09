#include <stdio.h>
#include <string.h>

#include "app_lift_core.h"
#include "app_io_map.h"
#include "fake_elog.h"
#include "fake_hal.h"
#include "fake_io.h"
#include "fake_w25qxx.h"
#include "test_assert.h"

static test_stats_t g_stats;

static void run_task_steps(unsigned int steps)
{
    for (unsigned int i = 0; i < steps; i++) {
        fake_hal_advance_ms(10U);
        App_LiftCore_Task();
    }
}

static void run_for_ms(unsigned int ms)
{
    unsigned int steps = (ms + 9U) / 10U;

    run_task_steps(steps);
}

static void set_input(io_in_id_t id, int active)
{
    if ((id == IO_IN_UPPER_LIMIT) ||
        (id == IO_IN_LOWER_LIMIT) ||
        (id == IO_IN_PHOTOELECTRIC)) {
        fake_io_set_raw(id, active ? 1U : 0U);
    } else {
        fake_io_set_raw(id, active ? 0U : 1U);
    }
}

static void reset_fixture(void)
{
    fake_hal_reset();
    fake_io_reset();
    fake_elog_reset();
    fake_w25qxx_reset();
    App_LiftCore_Init(PRODUCT_TYPE_SMALL_SCISSOR);
    fake_elog_reset();
}

static void expect_output(const char *label, int motor, int drop, int air)
{
    TEST_EXPECT(&g_stats,
                fake_io_get_out(IO_OUT_MOTOR) == (uint8_t)motor,
                "%s: expected motor=%d got %u",
                label,
                motor,
                fake_io_get_out(IO_OUT_MOTOR));
    TEST_EXPECT(&g_stats,
                fake_io_get_out(IO_OUT_DROP_VALVE) == (uint8_t)drop,
                "%s: expected drop=%d got %u",
                label,
                drop,
                fake_io_get_out(IO_OUT_DROP_VALVE));
    TEST_EXPECT(&g_stats,
                fake_io_get_out(IO_OUT_AIR_VALVE) == (uint8_t)air,
                "%s: expected air=%d got %u",
                label,
                air,
                fake_io_get_out(IO_OUT_AIR_VALVE));
}

static void expect_state(const char *label, const char *state)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();
    const char *actual = App_LiftCore_StateName(ctx->current_state);

    TEST_EXPECT(&g_stats,
                strcmp(actual, state) == 0,
                "%s: expected state=%s got %s",
                label,
                state,
                actual);
}

static void expect_log(const char *needle)
{
    TEST_EXPECT(&g_stats,
                fake_elog_contains(needle),
                "missing log containing: %s",
                needle);
}

static void start_case(const char *name)
{
    printf("[CASE] %s\n", name);
}

static void test_rise_air_after_delay_and_stop(void)
{
    start_case("rise_air_after_delay_and_stop");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(30U);
    expect_state("rise start", "rising");
    expect_output("rise start", 1, 0, 0);
    expect_log("PG15_up pressed");
    expect_log("idle -> rising");
    expect_log("op_up_start");
    expect_log("PF8 motor ON");

    run_for_ms(250U);
    expect_output("rise after 250ms air on", 1, 0, 1);
    expect_log("PD8 air_valve ON");

    set_input(IO_IN_UP_BUTTON, 0);
    run_for_ms(30U);
    expect_state("rise release", "idle");
    expect_output("rise release", 0, 0, 0);
    expect_log("op_up_stop_release");
}

static void test_rise_status_and_outputs(void)
{
    start_case("rise_status_and_outputs");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(30U);
    expect_state("rise status start", "rising");
    expect_output("rise status start", 1, 0, 0);
    expect_log("idle -> rising");

    run_for_ms(190U);
    expect_state("rise before air delay", "rising");
    expect_output("rise before air delay", 1, 0, 0);

    run_for_ms(20U);
    expect_state("rise after air delay", "rising");
    expect_output("rise after air delay", 1, 0, 1);
    expect_log("PD8 air_valve ON");

    set_input(IO_IN_UP_BUTTON, 0);
    run_for_ms(30U);
    expect_state("rise status release", "idle");
    expect_output("rise status release", 0, 0, 0);
}

static void test_rise_upper_limit(void)
{
    start_case("rise_upper_limit");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(30U);
    set_input(IO_IN_UPPER_LIMIT, 1);
    run_for_ms(30U);

    expect_state("rise upper limit", "idle");
    expect_output("rise upper limit", 0, 0, 0);
    expect_log("op_up_stop_limit");
}

static void test_down_sequence(void)
{
    start_case("down_sequence");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(30U);
    expect_state("down start", "down_prepare");
    expect_output("down start", 1, 0, 0);
    expect_log("PE0_down pressed");
    expect_log("op_down_start");

    run_for_ms(210U);
    expect_state("down air on", "down_hold_motor");
    expect_output("down air on", 1, 0, 1);
    expect_log("down_air_on");
    expect_log("PD8 air_valve ON");

    run_for_ms(3010U);
    expect_state("down dropping", "dropping");
    expect_output("down dropping", 0, 1, 1);
    expect_log("down_motor_hold_done");
    expect_log("PF8 motor OFF");
    expect_log("PF9 down_valve ON");

    set_input(IO_IN_DOWN_BUTTON, 0);
    run_for_ms(30U);
    expect_state("down release", "idle");
    expect_output("down release", 0, 0, 0);
    expect_log("op_down_stop_release");
}

static void test_down_state_transition_logs(void)
{
    start_case("down_state_transition_logs");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(30U);
    expect_state("down transition prepare", "down_prepare");
    expect_output("down transition prepare", 1, 0, 0);
    expect_log("idle -> down_prepare");

    run_for_ms(210U);
    expect_state("down transition hold motor", "down_hold_motor");
    expect_output("down transition hold motor", 1, 0, 1);
    expect_log("down_prepare -> down_hold_motor");
    expect_log("PD8 air_valve ON");

    run_for_ms(3010U);
    expect_state("down transition dropping", "dropping");
    expect_output("down transition dropping", 0, 1, 1);
    expect_log("down_hold_motor -> dropping");
    expect_log("PF8 motor OFF");
    expect_log("PF9 down_valve ON");

    set_input(IO_IN_DOWN_BUTTON, 0);
    run_for_ms(30U);
    expect_state("down transition release", "idle");
    expect_output("down transition release", 0, 0, 0);
    expect_log("dropping -> idle");
}

static void test_forced_fast_down_before_200ms(void)
{
    start_case("forced_fast_down_before_200ms");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(30U);
    set_input(IO_IN_UPPER_LIMIT, 1);
    run_for_ms(30U);

    expect_state("forced down before 200ms", "dropping");
    expect_output("forced down before 200ms", 0, 1, 1);
    expect_log("forced_fast_down");
}

static void test_forced_fast_down_after_200ms(void)
{
    start_case("forced_fast_down_after_200ms");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(240U);
    set_input(IO_IN_LOCK_BUTTON, 1);
    run_for_ms(30U);

    expect_state("forced down after 200ms", "dropping");
    expect_output("forced down after 200ms", 0, 1, 1);
    expect_log("forced_fast_down");
}

static void test_lock(void)
{
    start_case("lock");
    reset_fixture();

    set_input(IO_IN_LOCK_BUTTON, 1);
    run_for_ms(30U);
    expect_state("lock start", "locking");
    expect_output("lock start", 0, 1, 0);
    expect_log("op_lock_start");

    set_input(IO_IN_LOCK_BUTTON, 0);
    run_for_ms(30U);
    expect_state("lock release", "idle");
    expect_output("lock release", 0, 0, 0);
    expect_log("op_lock_stop");
}

static void test_refill(void)
{
    start_case("refill");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    set_input(IO_IN_REFILL_BUTTON, 1);
    run_for_ms(30U);
    expect_state("refill start", "refill");
    expect_output("refill start", 1, 0, 0);
    expect_log("op_refill_start");

    run_for_ms(210U);
    expect_output("refill air delay", 1, 0, 1);

    set_input(IO_IN_REFILL_BUTTON, 0);
    run_for_ms(30U);
    expect_state("refill release", "idle");
    expect_output("refill release", 0, 0, 0);
    expect_log("op_refill_stop");
}

static void test_refill_ignores_upper_limit(void)
{
    start_case("refill_ignores_upper_limit");
    reset_fixture();

    set_input(IO_IN_UPPER_LIMIT, 1);
    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(30U);
    expect_state("upper blocks normal up", "idle");
    expect_output("upper blocks normal up", 0, 0, 0);

    set_input(IO_IN_REFILL_BUTTON, 1);
    run_for_ms(30U);
    expect_state("refill despite upper", "refill");
    expect_output("refill despite upper", 1, 0, 0);

    run_for_ms(210U);
    expect_state("refill upper held", "refill");
    expect_output("refill upper held", 1, 0, 1);

    set_input(IO_IN_UP_BUTTON, 0);
    run_for_ms(30U);
    expect_state("refill up release", "idle");
    expect_output("refill up release", 0, 0, 0);
}

static void test_refill_button_alone_no_effect(void)
{
    start_case("refill_button_alone_no_effect");
    reset_fixture();

    set_input(IO_IN_REFILL_BUTTON, 1);
    run_for_ms(30U);
    expect_state("refill alone", "idle");
    expect_output("refill alone", 0, 0, 0);
}

static void test_estop(void)
{
    start_case("estop");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(240U);
    expect_output("estop setup", 1, 0, 1);

    set_input(IO_IN_ESTOP, 1);
    run_for_ms(30U);
    expect_state("estop active", "estop");
    expect_output("estop active", 0, 0, 0);
    expect_log("SAFE] estop active");
    expect_log("EVENT_ESTOP");
}

static void test_estop_during_rise_after_air_on(void)
{
    start_case("estop_during_rise_after_air_on");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(280U);
    expect_state("estop rise setup", "rising");
    expect_output("estop rise setup", 1, 0, 1);

    set_input(IO_IN_ESTOP, 1);
    run_for_ms(30U);
    expect_state("estop during rise", "estop");
    expect_output("estop during rise", 0, 0, 0);
    expect_log("rising -> estop");
    expect_log("SAFE] estop active");
    expect_log("EVENT_ESTOP");
}

static void test_estop_requires_button_release_to_recover(void)
{
    start_case("estop_requires_button_release_to_recover");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(280U);
    expect_state("estop recovery setup", "rising");
    expect_output("estop recovery setup", 1, 0, 1);

    set_input(IO_IN_ESTOP, 1);
    run_for_ms(30U);
    expect_state("estop recovery active", "estop");
    expect_output("estop recovery active", 0, 0, 0);

    set_input(IO_IN_ESTOP, 0);
    run_for_ms(30U);
    expect_state("estop held by up button", "estop");
    expect_output("estop held by up button", 0, 0, 0);

    set_input(IO_IN_UP_BUTTON, 0);
    run_for_ms(30U);
    expect_state("estop recovered after release", "idle");
    expect_output("estop recovered after release", 0, 0, 0);
    expect_log("EVENT_ESTOP_RECOVER");
    expect_log("estop_recovered");
}

static void test_photo_alarm_and_clear(void)
{
    start_case("photo_alarm_and_clear");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(240U);
    expect_output("photo setup", 1, 0, 1);

    set_input(IO_IN_PHOTOELECTRIC, 1);
    run_for_ms(60U);
    expect_state("photo alarm", "photo_alarm");
    expect_output("photo alarm", 0, 0, 0);
    expect_log("EVENT_PHOTO_ALARM");
    expect_log("photo alarm latched");

    set_input(IO_IN_DOWN_BUTTON, 0);
    set_input(IO_IN_PHOTOELECTRIC, 0);
    run_for_ms(70U);
    App_LiftCore_RequestClearPhotoAlarm("test");
    run_for_ms(10U);
    expect_state("photo cleared", "idle");
    expect_output("photo cleared", 0, 0, 0);
    expect_log("photo_alarm_cleared");
}

static void test_idle_photo_alarm_auto_clears_after_release(void)
{
    start_case("idle_photo_alarm_auto_clears_after_release");
    reset_fixture();

    set_input(IO_IN_PHOTOELECTRIC, 1);
    run_for_ms(70U);
    expect_state("idle photo alarm", "photo_alarm");
    expect_output("idle photo alarm", 0, 0, 0);

    set_input(IO_IN_PHOTOELECTRIC, 0);
    run_for_ms(70U);
    expect_state("idle photo auto clear", "idle");
    expect_output("idle photo auto clear", 0, 0, 0);
    expect_log("photo_alarm_auto_cleared");
}

static void test_motion_photo_alarm_requires_remote_clear(void)
{
    start_case("motion_photo_alarm_requires_remote_clear");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    run_for_ms(280U);
    expect_state("motion photo setup", "rising");
    expect_output("motion photo setup", 1, 0, 1);

    set_input(IO_IN_PHOTOELECTRIC, 1);
    run_for_ms(70U);
    expect_state("motion photo alarm", "photo_alarm");
    expect_output("motion photo alarm", 0, 0, 0);

    set_input(IO_IN_UP_BUTTON, 0);
    set_input(IO_IN_PHOTOELECTRIC, 0);
    run_for_ms(70U);
    expect_state("motion photo still latched", "photo_alarm");
    expect_output("motion photo still latched", 0, 0, 0);

    App_LiftCore_RequestClearPhotoAlarm("test");
    run_for_ms(10U);
    expect_state("motion photo remote clear", "idle");
    expect_output("motion photo remote clear", 0, 0, 0);
    expect_log("photo_alarm_cleared");
}

static void test_lower_limit_masks_photo_alarm(void)
{
    start_case("lower_limit_masks_photo_alarm");
    reset_fixture();

    set_input(IO_IN_LOWER_LIMIT, 1);
    set_input(IO_IN_PHOTOELECTRIC, 1);
    run_for_ms(70U);

    expect_state("lower masks photo state", "idle");
    expect_output("lower masks photo output", 0, 0, 0);
    TEST_EXPECT(&g_stats,
                !fake_elog_contains("EVENT_PHOTO_ALARM"),
                "photo alarm should be masked by lower limit");
}

static void test_lower_limit_clears_photo_alarm(void)
{
    start_case("lower_limit_clears_photo_alarm");
    reset_fixture();

    set_input(IO_IN_PHOTOELECTRIC, 1);
    run_for_ms(70U);
    expect_state("photo alarm before lower", "photo_alarm");

    set_input(IO_IN_LOWER_LIMIT, 1);
    run_for_ms(30U);
    expect_state("photo alarm cleared by lower", "idle");
    expect_output("photo alarm cleared by lower", 0, 0, 0);
    expect_log("photo_alarm_cleared_by_lower");
}

static void test_lower_limit_does_not_change_down_outputs(void)
{
    start_case("lower_limit_does_not_change_down_outputs");
    reset_fixture();

    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(240U);
    expect_state("down before lower", "down_hold_motor");
    expect_output("down before lower", 1, 0, 1);

    set_input(IO_IN_LOWER_LIMIT, 1);
    set_input(IO_IN_PHOTOELECTRIC, 1);
    run_for_ms(60U);
    expect_state("down lower masks photo", "down_hold_motor");
    expect_output("down lower masks photo", 1, 0, 1);
    TEST_EXPECT(&g_stats,
                !fake_elog_contains("EVENT_PHOTO_ALARM"),
                "lower limit should not change down outputs or allow photo alarm");
}

static void test_up_down_interlock(void)
{
    start_case("up_down_interlock");
    reset_fixture();

    set_input(IO_IN_UP_BUTTON, 1);
    set_input(IO_IN_DOWN_BUTTON, 1);
    run_for_ms(30U);

    expect_state("up down interlock", "safe_stop");
    expect_output("up down interlock", 0, 0, 0);
    expect_log("EVENT_INVALID_INPUT");
    expect_log("up and down pressed together");
}

int main(void)
{
    /*
     * Tests are disabled by user request. Keep the cases and log assertions in
     * source so this host runner can be restored without rebuilding coverage.
     */
    test_rise_air_after_delay_and_stop();
    test_rise_status_and_outputs();
    test_rise_upper_limit();
    test_down_sequence();
    test_down_state_transition_logs();
    test_forced_fast_down_before_200ms();
    test_forced_fast_down_after_200ms();
    test_lock();
    test_refill();
    test_refill_ignores_upper_limit();
    test_refill_button_alone_no_effect();
    test_estop();
    test_estop_during_rise_after_air_on();
    test_estop_requires_button_release_to_recover();
    test_photo_alarm_and_clear();
    test_idle_photo_alarm_auto_clears_after_release();
    test_motion_photo_alarm_requires_remote_clear();
    test_lower_limit_masks_photo_alarm();
    test_lower_limit_clears_photo_alarm();
    test_lower_limit_does_not_change_down_outputs();
    test_up_down_interlock();

    printf("[RESULT] passed=%d failed=%d\n", g_stats.passed, g_stats.failed);

    return (g_stats.failed == 0) ? 0 : 1;
}
