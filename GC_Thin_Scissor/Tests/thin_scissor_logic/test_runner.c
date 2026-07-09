#include <stdio.h>
#include <string.h>

#include "fake_runtime.h"
#include "lift_core.h"
#include "test_assert.h"

extern const lift_ops_t thin_scissor_ops;

static test_stats_t g_stats;

static void step_ms(uint32_t ms)
{
    uint32_t elapsed = 0U;
    while (elapsed < ms) {
        fake_advance_ms(10U);
        thin_scissor_ops.poll();
        elapsed += 10U;
    }
}

static void expect_outputs(const char *label, int motor, int drop, int air)
{
    TEST_EXPECT(&g_stats, fake_output(IO_OUT_MOTOR) == (uint8_t)motor,
                "%s motor expected=%d got=%u", label, motor, fake_output(IO_OUT_MOTOR));
    TEST_EXPECT(&g_stats, fake_output(IO_OUT_DROP_VALVE) == (uint8_t)drop,
                "%s drop expected=%d got=%u", label, drop, fake_output(IO_OUT_DROP_VALVE));
    TEST_EXPECT(&g_stats, fake_output(IO_OUT_MAIN_AIR_VALVE) == (uint8_t)air,
                "%s air expected=%d got=%u", label, air, fake_output(IO_OUT_MAIN_AIR_VALVE));
}

static void expect_state(const char *label, lift_state_t state)
{
    TEST_EXPECT(&g_stats, g_lift_state == state,
                "%s state expected=%d got=%d", label, (int)state, (int)g_lift_state);
}

static void reset_case(const char *name)
{
    printf("[CASE] %s\n", name);
    fake_reset();
    thin_scissor_ops.init();
}

static void test_up_button_starts_motor(void)
{
    reset_case("up_button_starts_motor");
    fake_set_input(IO_IN_UP_BUTTON, 1U);
    step_ms(30U);
    expect_state("up start", LIFT_STATE_RISING);
    expect_outputs("up start", 1, 0, 0);

    step_ms(250U);
    expect_outputs("up after 250ms", 1, 0, 0);
    TEST_EXPECT(&g_stats, fake_log_contains("UP start"), "missing UP start log");

    fake_set_input(IO_IN_UP_BUTTON, 0U);
    step_ms(30U);
    expect_state("up release", LIFT_STATE_IDLE);
    expect_outputs("up release", 0, 0, 0);
}

static void test_up_upper_limit(void)
{
    reset_case("up_upper_limit");
    fake_set_input(IO_IN_UP_BUTTON, 1U);
    step_ms(30U);
    fake_set_input(IO_IN_UPPER_LIMIT, 1U);
    step_ms(30U);
    expect_state("up upper limit", LIFT_STATE_IDLE);
    expect_outputs("up upper limit", 0, 0, 0);
    TEST_EXPECT(&g_stats, fake_log_contains("upper_limit"), "missing upper limit log");
}

static void test_down_sequence(void)
{
    reset_case("down_sequence");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(30U);
    expect_state("down start", LIFT_STATE_DROPPING);
    expect_outputs("down start", 1, 0, 0);

    step_ms(210U);
    expect_outputs("down air on", 1, 0, 1);

    step_ms(2770U);
    expect_outputs("down dropping", 0, 1, 1);
    TEST_EXPECT(&g_stats, fake_log_contains("DOWN motor=OFF"), "missing motor hold done log");

    fake_set_input(IO_IN_DOWN_BUTTON, 0U);
    step_ms(30U);
    expect_state("down release", LIFT_STATE_IDLE);
    expect_outputs("down release", 0, 0, 0);
}

static void test_forced_fast_down_before_delay(void)
{
    reset_case("forced_fast_down_before_delay");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(30U);
    fake_set_input(IO_IN_UPPER_LIMIT, 1U);
    step_ms(30U);
    expect_state("forced fast down", LIFT_STATE_DROPPING);
    expect_outputs("forced fast down", 0, 1, 1);
    TEST_EXPECT(&g_stats, fake_log_contains("forced_fast_down"), "missing forced_fast_down log");
}

static void test_forced_fast_down_by_lock(void)
{
    reset_case("forced_fast_down_by_lock");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(240U);
    fake_set_input(IO_IN_LOCK_BUTTON, 1U);
    step_ms(30U);
    expect_outputs("lock forced fast down", 0, 1, 1);
}

static void test_lower_limit_drop_only_before_delay(void)
{
    reset_case("lower_limit_drop_only_before_delay");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(30U);
    fake_set_input(IO_IN_LOWER_LIMIT, 1U);
    step_ms(30U);
    expect_state("lower before delay", LIFT_STATE_DROPPING);
    expect_outputs("lower before delay", 0, 1, 0);
    TEST_EXPECT(&g_stats, fake_log_contains("lower_limit"), "missing lower limit log");
}

static void test_lower_limit_drop_only_hold_motor(void)
{
    reset_case("lower_limit_drop_only_hold_motor");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(240U);
    fake_set_input(IO_IN_LOWER_LIMIT, 1U);
    step_ms(30U);
    expect_state("lower hold motor", LIFT_STATE_DROPPING);
    expect_outputs("lower hold motor", 0, 1, 0);
    TEST_EXPECT(&g_stats, fake_log_contains("lower_limit"), "missing lower limit log");
}

static void test_lower_limit_drop_only_after_dropping(void)
{
    reset_case("lower_limit_drop_only_after_dropping");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(3240U);
    expect_outputs("dropping before lower", 0, 1, 1);
    fake_set_input(IO_IN_LOWER_LIMIT, 1U);
    step_ms(30U);
    expect_state("lower after dropping", LIFT_STATE_DROPPING);
    expect_outputs("lower after dropping", 0, 1, 0);
    TEST_EXPECT(&g_stats, fake_log_contains("lower_limit"), "missing lower limit log");
}

static void test_lock_and_refill(void)
{
    reset_case("lock_and_refill");
    fake_set_input(IO_IN_LOCK_BUTTON, 1U);
    step_ms(30U);
    expect_state("lock start", LIFT_STATE_LOCKED);
    expect_outputs("lock start", 0, 1, 1);
    fake_set_input(IO_IN_LOCK_BUTTON, 0U);
    step_ms(30U);
    expect_outputs("lock release", 0, 0, 0);

    fake_set_input(IO_IN_LOWER_LIMIT, 1U);
    fake_set_input(IO_IN_LOCK_BUTTON, 1U);
    step_ms(30U);
    expect_state("lock lower start", LIFT_STATE_LOCKED);
    expect_outputs("lock lower start", 0, 1, 0);
    fake_set_input(IO_IN_LOWER_LIMIT, 0U);
    step_ms(30U);
    expect_outputs("lock lower released", 0, 1, 1);
    fake_set_input(IO_IN_LOCK_BUTTON, 0U);
    step_ms(30U);
    expect_outputs("lock lower release", 0, 0, 0);

    fake_set_input(IO_IN_UP_BUTTON, 1U);
    fake_set_input(IO_IN_REFILL_BUTTON, 1U);
    step_ms(30U);
    expect_state("refill start", LIFT_STATE_REFILLING);
    expect_outputs("refill start", 1, 0, 0);
    fake_set_input(IO_IN_REFILL_BUTTON, 0U);
    step_ms(30U);
    expect_state("refill release", LIFT_STATE_IDLE);
    expect_outputs("refill release", 0, 0, 0);
    fake_set_input(IO_IN_UP_BUTTON, 0U);
    step_ms(30U);
    expect_outputs("up release stops", 0, 0, 0);
}

static void test_refill_ignores_upper_and_refill_alone_idle(void)
{
    reset_case("refill_ignores_upper_and_refill_alone_idle");

    fake_set_input(IO_IN_REFILL_BUTTON, 1U);
    step_ms(30U);
    expect_state("refill alone state", LIFT_STATE_IDLE);
    expect_outputs("refill alone outputs", 0, 0, 0);
    fake_set_input(IO_IN_REFILL_BUTTON, 0U);
    step_ms(30U);

    fake_set_input(IO_IN_UPPER_LIMIT, 1U);
    fake_set_input(IO_IN_UP_BUTTON, 1U);
    step_ms(30U);
    expect_state("upper blocks normal up", LIFT_STATE_IDLE);
    expect_outputs("upper blocks normal up", 0, 0, 0);

    fake_set_input(IO_IN_REFILL_BUTTON, 1U);
    step_ms(30U);
    expect_state("refill despite upper", LIFT_STATE_REFILLING);
    expect_outputs("refill despite upper", 1, 0, 0);

    fake_set_input(IO_IN_REFILL_BUTTON, 0U);
    step_ms(30U);
    expect_state("refill upper release", LIFT_STATE_IDLE);
    expect_outputs("refill upper release", 0, 0, 0);
}

static void reset_core_case(const char *name)
{
    printf("[CASE] %s\n", name);
    fake_reset();
    LiftCore_Init();
}

static void test_lower_limit_masks_photo_alarm(void)
{
    reset_core_case("lower_limit_masks_photo_alarm");
    fake_set_input(IO_IN_LOWER_LIMIT, 1U);
    fake_set_input(IO_IN_PHOTOELECTRIC, 1U);
    LiftCore_Poll();
    expect_state("lower masks photo", LIFT_STATE_IDLE);
    expect_outputs("lower masks photo", 0, 0, 0);
}

static void test_lower_limit_clears_photo_alarm(void)
{
    reset_core_case("lower_limit_clears_photo_alarm");
    fake_set_input(IO_IN_PHOTOELECTRIC, 1U);
    LiftCore_Poll();
    expect_state("photo alarm before lower", LIFT_STATE_PHOTO_ALARM);
    expect_outputs("photo alarm before lower", 0, 0, 0);

    fake_set_input(IO_IN_LOWER_LIMIT, 1U);
    LiftCore_Poll();
    expect_state("photo alarm cleared by lower", LIFT_STATE_IDLE);
    expect_outputs("photo alarm cleared by lower", 0, 0, 0);
}

static void test_idle_photo_alarm_auto_clears_after_release(void)
{
    reset_core_case("idle_photo_alarm_auto_clears_after_release");
    fake_set_input(IO_IN_PHOTOELECTRIC, 1U);
    LiftCore_Poll();
    expect_state("idle photo alarm", LIFT_STATE_PHOTO_ALARM);
    expect_outputs("idle photo alarm", 0, 0, 0);

    fake_set_input(IO_IN_PHOTOELECTRIC, 0U);
    LiftCore_Poll();
    expect_state("idle photo alarm auto clear", LIFT_STATE_IDLE);
    expect_outputs("idle photo alarm auto clear", 0, 0, 0);
}

static void test_motion_photo_alarm_requires_remote_clear(void)
{
    reset_core_case("motion_photo_alarm_requires_remote_clear");
    fake_set_input(IO_IN_UP_BUTTON, 1U);
    LiftCore_Poll();
    expect_state("motion photo setup", LIFT_STATE_RISING);

    fake_set_input(IO_IN_PHOTOELECTRIC, 1U);
    LiftCore_Poll();
    expect_state("motion photo alarm", LIFT_STATE_PHOTO_ALARM);
    expect_outputs("motion photo alarm", 0, 0, 0);

    fake_set_input(IO_IN_UP_BUTTON, 0U);
    fake_set_input(IO_IN_PHOTOELECTRIC, 0U);
    LiftCore_Poll();
    expect_state("motion photo still latched", LIFT_STATE_PHOTO_ALARM);

    LiftCore_ClearAlarm();
    expect_state("motion photo remote clear", LIFT_STATE_IDLE);
    expect_outputs("motion photo remote clear", 0, 0, 0);
}

static void test_safety_callbacks(void)
{
    reset_case("safety_callbacks");
    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(240U);
    thin_scissor_ops.on_estop();
    expect_outputs("estop callback", 0, 0, 0);

    fake_set_input(IO_IN_DOWN_BUTTON, 1U);
    step_ms(240U);
    thin_scissor_ops.on_photoelectric_blocked();
    expect_outputs("photo callback", 0, 0, 0);
}

int main(void)
{
    /*
     * Tests are disabled by user request. Keep the cases and log assertions in
     * source so this host runner can be restored without rebuilding coverage.
     */
    test_up_button_starts_motor();
    test_up_upper_limit();
    test_down_sequence();
    test_forced_fast_down_before_delay();
    test_forced_fast_down_by_lock();
    test_lower_limit_drop_only_before_delay();
    test_lower_limit_drop_only_hold_motor();
    test_lower_limit_drop_only_after_dropping();
    test_lock_and_refill();
    test_refill_ignores_upper_and_refill_alone_idle();
    test_lower_limit_masks_photo_alarm();
    test_lower_limit_clears_photo_alarm();
    test_idle_photo_alarm_auto_clears_after_release();
    test_motion_photo_alarm_requires_remote_clear();
    test_safety_callbacks();

    printf("passed=%d failed=%d\n", g_stats.passed, g_stats.failed);
    return (g_stats.failed == 0) ? 0 : 1;
}
