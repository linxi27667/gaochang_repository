#include <stdio.h>
#include <string.h>

#include "fake_runtime.h"
#include "test_assert.h"
#include "app_w25qxx.h"
#include "lift_iot.h"

static test_stats_t g_test_stats;

static void begin_rise(void)
{
    fake_set_state(LIFT_STATE_RISING);
    LiftIot_Poll();
}

static void end_rise(void)
{
    fake_set_state(LIFT_STATE_IDLE);
    LiftIot_Poll();
}

static void add_rise_ms(uint32_t ms)
{
    fake_advance_ms(ms);
    LiftIot_Poll();
}

static void test_below_threshold(void)
{
    printf("[CASE] below_threshold\n");
    fake_reset();

    begin_rise();
    add_rise_ms(2990U);
    end_rise();

    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 0U,
                "2990ms total=%lu", (unsigned long)g_rise_stats.total_up_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.pending_count == 0U,
                "2990ms pending=%lu", (unsigned long)g_rise_stats.pending_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.remainder_ms == 2990U,
                "2990ms remainder=%u", (unsigned int)g_rise_stats.remainder_ms);
}

static void test_exact_threshold_and_restore(void)
{
    printf("[CASE] exact_threshold_and_restore\n");
    fake_reset();

    begin_rise();
    add_rise_ms(3000U);

    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 1U,
                "3000ms total=%lu", (unsigned long)g_rise_stats.total_up_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.pending_count == 1U,
                "3000ms pending=%lu", (unsigned long)g_rise_stats.pending_count);
    TEST_EXPECT(&g_test_stats, g_stats.up_count == 1U,
                "legacy up=%lu", (unsigned long)g_stats.up_count);

    fake_reboot();
    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 1U,
                "restored total=%lu", (unsigned long)g_rise_stats.total_up_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.pending_count == 1U,
                "restored pending=%lu", (unsigned long)g_rise_stats.pending_count);
}

static void test_cross_action_accumulation(void)
{
    printf("[CASE] cross_action_accumulation\n");
    fake_reset();

    begin_rise();
    add_rise_ms(1400U);
    end_rise();
    begin_rise();
    add_rise_ms(1600U);

    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 1U,
                "cross total=%lu", (unsigned long)g_rise_stats.total_up_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.remainder_ms == 0U,
                "cross remainder=%u", (unsigned int)g_rise_stats.remainder_ms);
}

static void test_refill_requires_up_button(void)
{
    printf("[CASE] refill_requires_up_button\n");
    fake_reset();

    fake_set_state(LIFT_STATE_REFILLING);
    add_rise_ms(3000U);
    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 0U,
                "refill-only total=%lu", (unsigned long)g_rise_stats.total_up_count);

    fake_set_input(IO_IN_UP_BUTTON, 1U);
    fake_set_input(IO_IN_REFILL_BUTTON, 1U);
    LiftIot_Poll();
    add_rise_ms(3000U);
    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 1U,
                "up+refill total=%lu", (unsigned long)g_rise_stats.total_up_count);
}

static void test_confirm_clears_pending(void)
{
    char json[512];

    printf("[CASE] confirm_clears_pending\n");
    fake_reset();
    begin_rise();
    add_rise_ms(3000U);

    TEST_EXPECT(&g_test_stats, fake_map_chip_uid() != 0,
                "unable to map fake STM32 UID address");
    TEST_EXPECT(&g_test_stats, LiftIot_BuildRiseCountJson(json, sizeof(json)) == LIFT_IOT_OK,
                "rise JSON build failed");
    TEST_EXPECT(&g_test_stats, LiftIot_ConfirmPendingRiseReport() != 0U,
                "rise confirmation failed");
    TEST_EXPECT(&g_test_stats, g_rise_stats.pending_count == 0U,
                "confirmed pending=%lu", (unsigned long)g_rise_stats.pending_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.next_sequence == 2U,
                "confirmed sequence=%lu", (unsigned long)g_rise_stats.next_sequence);

    fake_reboot();
    TEST_EXPECT(&g_test_stats, g_rise_stats.total_up_count == 1U,
                "confirmed restore total=%lu", (unsigned long)g_rise_stats.total_up_count);
    TEST_EXPECT(&g_test_stats, g_rise_stats.pending_count == 0U,
                "confirmed restore pending=%lu", (unsigned long)g_rise_stats.pending_count);
}

static void test_maintenance_threshold(void)
{
    printf("[CASE] maintenance_4999_5000\n");
    fake_reset();

    App_W25Qxx_Maintenance_AddLiftUnits(4999U);
    TEST_EXPECT(&g_test_stats, App_W25Qxx_Maintenance_Save() == W25Q_OK,
                "save 4999 failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.maintenance_due == 0U,
                "4999 marked due");

    App_W25Qxx_Maintenance_AddLiftUnits(1U);
    TEST_EXPECT(&g_test_stats, App_W25Qxx_Maintenance_Save() == W25Q_OK,
                "save 5000 failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.total_lift_count == 5000U,
                "total=%lu", (unsigned long)g_maintenance.total_lift_count);
    TEST_EXPECT(&g_test_stats, g_maintenance.maintenance_due == 1U,
                "5000 not marked due");
}

static void test_maintenance_done_and_usage_reset(void)
{
    printf("[CASE] maintenance_done_and_usage_reset\n");
    fake_reset();
    App_W25Qxx_Maintenance_AddLiftUnits(5000U);
    TEST_EXPECT(&g_test_stats, App_W25Qxx_Maintenance_Save() == W25Q_OK,
                "setup save failed");

    TEST_EXPECT(&g_test_stats, LiftIot_MaintenanceDone("test", "maint-001") == LIFT_IOT_OK,
                "maintenance done failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.total_lift_count == 5000U,
                "maintenance changed total");
    TEST_EXPECT(&g_test_stats, (g_maintenance.maintenance_count == 1U) &&
                (g_maintenance.maintenance_lift_count == 0U) &&
                (g_maintenance.maintenance_due == 0U), "maintenance state incorrect");

    fake_reboot();
    TEST_EXPECT(&g_test_stats, (g_maintenance.total_lift_count == 5000U) &&
                (g_maintenance.maintenance_count == 1U), "maintenance restore failed");
    TEST_EXPECT(&g_test_stats, LiftIot_MaintenanceDone("test", "maint-001") == LIFT_IOT_OK,
                "maintenance replay failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.maintenance_count == 1U,
                "maintenance replay incremented count");

    TEST_EXPECT(&g_test_stats, LiftIot_ResetUsage("test", "reset-001") == LIFT_IOT_OK,
                "usage reset failed");
    TEST_EXPECT(&g_test_stats, (g_maintenance.total_lift_count == 0U) &&
                (g_maintenance.maintenance_lift_count == 0U) &&
                (g_maintenance.maintenance_count == 0U) &&
                (g_maintenance.usage_epoch == 1U), "usage reset fields incorrect");
    fake_reboot();
    TEST_EXPECT(&g_test_stats, LiftIot_ResetUsage("test", "reset-001") == LIFT_IOT_OK,
                "usage reset replay failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.usage_epoch == 1U,
                "usage reset replay advanced epoch");
    TEST_EXPECT(&g_test_stats, (g_rise_stats.total_up_count == 0U) &&
                (g_rise_stats.pending_count == 0U), "usage reset did not clear pending rise data");
}

static void test_maintenance_power_restore_and_crc(void)
{
    printf("[CASE] maintenance_power_restore_and_crc\n");
    fake_reset();
    begin_rise();
    add_rise_ms(3000U);
    fake_reboot();
    TEST_EXPECT(&g_test_stats, g_maintenance.total_lift_count == 1U,
                "maintenance power restore total=%lu", (unsigned long)g_maintenance.total_lift_count);

    fake_reset();
    App_W25Qxx_Maintenance_AddLiftUnits(1U);
    TEST_EXPECT(&g_test_stats, App_W25Qxx_Maintenance_Save() == W25Q_OK,
                "crc setup save failed");
    fake_corrupt_flash(W25Q_MAINTENANCE_JOURNAL_ADDR + 40U);
    TEST_EXPECT(&g_test_stats, App_W25Qxx_Maintenance_Load() == W25Q_OK,
                "crc load failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.total_lift_count == 0U,
                "corrupt record was accepted");
}

static void test_old_epoch_rise_report_is_invalidated(void)
{
    char json[512];

    printf("[CASE] old_epoch_rise_report_is_invalidated\n");
    fake_reset();
    begin_rise();
    add_rise_ms(3000U);
    TEST_EXPECT(&g_test_stats, LiftIot_BuildRiseCountJson(json, sizeof(json)) == LIFT_IOT_OK,
                "old epoch rise JSON build failed");
    TEST_EXPECT(&g_test_stats, strstr(json, "\"usage_epoch\":0") != NULL,
                "old report does not carry epoch zero");

    TEST_EXPECT(&g_test_stats, LiftIot_ResetUsage("test", "reset-epoch") == LIFT_IOT_OK,
                "usage reset failed");
    TEST_EXPECT(&g_test_stats, g_maintenance.usage_epoch == 1U,
                "usage epoch did not advance");
    TEST_EXPECT(&g_test_stats, LiftIot_ConfirmPendingRiseReport() == 0U,
                "old epoch report remained confirmable");
}

int main(void)
{
    test_below_threshold();
    test_exact_threshold_and_restore();
    test_cross_action_accumulation();
    test_refill_requires_up_button();
    test_confirm_clears_pending();
    test_maintenance_threshold();
    test_maintenance_done_and_usage_reset();
    test_maintenance_power_restore_and_crc();
    test_old_epoch_rise_report_is_invalidated();

    printf("passed=%d failed=%d\n", g_test_stats.passed, g_test_stats.failed);
    return (g_test_stats.failed == 0) ? 0 : 1;
}
