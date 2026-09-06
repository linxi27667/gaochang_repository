#include "dri_min_sys.h"
#include "app_fram.h"
#include "app_min_sys.h"
#include "elog.h"
#include "task.h"

volatile uint8_t g_min_sys_task_created;
volatile uint8_t g_min_sys_task_started;
volatile uint32_t g_min_sys_task_loop_count;

static void MinSys_Log_Input_Changes(uint16_t current_bitmap,
                                     uint16_t *last_bitmap);
static void MinSys_Set_All_Outputs(uint8_t active);
static void MinSys_Fram_Test(void);

BaseType_t MinSys_Task_Create(void)
{
    BaseType_t result;

    elog_i("TASK", "[TASK] create min_sys stack_words=%lu heap_total=%lu",
           (unsigned long)MIN_SYS_TASK_STACK_WORDS,
           (unsigned long)configTOTAL_HEAP_SIZE);

    result = xTaskCreate(MinSys_Task,
                         "min_sys",
                         MIN_SYS_TASK_STACK_WORDS,
                         0,
                         MIN_SYS_TASK_PRIORITY,
                         0);
    g_min_sys_task_created = (result == pdPASS) ? 1U : 0U;

    if (result == pdPASS)
    {
        elog_a("TASK", "[TASK] min_sys task created heap_free=%lu",
               (unsigned long)xPortGetFreeHeapSize());
    }
    else
    {
        elog_e("TASK", "[TASK] min_sys task create failed heap_free=%lu",
               (unsigned long)xPortGetFreeHeapSize());
    }

    return result;
}

void MinSys_Task(void *pv_parameters)
{
    TickType_t last_wake_tick;
    uint16_t last_input_bitmap = 0xFFFFU;
    uint32_t led_elapsed_ms = 0U;
    uint32_t heartbeat_elapsed_ms = 0U;
    uint32_t output_elapsed_ms = 0U;
    uint8_t outputs_active = 0U;
    bsp_min_sys_snapshot_t snapshot = {0};

    (void)pv_parameters;
    MinSys_Safe_Off();
    MinSys_Set_Run_Led(0U);
    g_min_sys_task_started = 1U;

    elog_a("SYS", "[SYS] min_sys task started output_test=%u",
           (unsigned int)MIN_SYS_OUTPUT_TEST_ENABLE);

    MinSys_Fram_Test();
#if MIN_SYS_OUTPUT_TEST_ENABLE == 1U
    outputs_active = 1U;
    MinSys_Set_All_Outputs(outputs_active);
    elog_w("OUTPUT", "[OUTPUT] ALL ON out=0x0F relay=0x3F duration=%lums",
           (unsigned long)MIN_SYS_OUTPUT_TEST_ON_MS);
#endif
    last_wake_tick = xTaskGetTickCount();

    while (1)
    {
        if (MinSys_Sample() != 0U)
        {
            MinSys_Get_Snapshot(&snapshot);
            MinSys_Log_Input_Changes(snapshot.input_bitmap, &last_input_bitmap);
        }

        led_elapsed_ms += MIN_SYS_TASK_PERIOD_MS;
        if (led_elapsed_ms >= MIN_SYS_LED_PERIOD_MS)
        {
            led_elapsed_ms = 0U;
            MinSys_Toggle_Run_Led();
        }

        heartbeat_elapsed_ms += MIN_SYS_TASK_PERIOD_MS;
        if (heartbeat_elapsed_ms >= MIN_SYS_HEARTBEAT_PERIOD_MS)
        {
            heartbeat_elapsed_ms = 0U;
            elog_i("SYS",
                   "[SYS] hb loop=%lu inputs=0x%03X adc=%u adc12=%lu.%03luV heap=%lu min_heap=%lu stack_hwm=%lu",
                   (unsigned long)g_min_sys_task_loop_count,
                   (unsigned int)snapshot.input_bitmap,
                   (unsigned int)snapshot.adc_raw,
                   (unsigned long)(snapshot.adc_12v_mv / 1000UL),
                   (unsigned long)(snapshot.adc_12v_mv % 1000UL),
                   (unsigned long)xPortGetFreeHeapSize(),
                   (unsigned long)xPortGetMinimumEverFreeHeapSize(),
                   (unsigned long)uxTaskGetStackHighWaterMark(0));
        }

#if MIN_SYS_OUTPUT_TEST_ENABLE == 1U
        output_elapsed_ms += MIN_SYS_TASK_PERIOD_MS;
        if (((outputs_active != 0U) &&
             (output_elapsed_ms >= MIN_SYS_OUTPUT_TEST_ON_MS)) ||
            ((outputs_active == 0U) &&
             (output_elapsed_ms >= MIN_SYS_OUTPUT_TEST_OFF_MS)))
        {
            output_elapsed_ms = 0U;
            outputs_active = (outputs_active == 0U) ? 1U : 0U;
            MinSys_Set_All_Outputs(outputs_active);
            elog_w("OUTPUT",
                   "[OUTPUT] ALL %s out=0x%02X relay=0x%02X duration=%lums",
                   (outputs_active != 0U) ? "ON" : "OFF",
                   (outputs_active != 0U) ? 0x0FU : 0U,
                   (outputs_active != 0U) ? 0x3FU : 0U,
                   (unsigned long)((outputs_active != 0U) ?
                                   MIN_SYS_OUTPUT_TEST_ON_MS :
                                   MIN_SYS_OUTPUT_TEST_OFF_MS));
        }
#endif

        g_min_sys_task_loop_count++;
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(MIN_SYS_TASK_PERIOD_MS));
    }
}

static void MinSys_Log_Input_Changes(uint16_t current_bitmap,
                                     uint16_t *last_bitmap)
{
    uint16_t changed_bitmap;
    uint8_t index;

    if (last_bitmap == 0)
    {
        return;
    }

    changed_bitmap = (uint16_t)(current_bitmap ^ *last_bitmap);
    if (changed_bitmap == 0U)
    {
        return;
    }

    for (index = 0U; index < BSP_MIN_SYS_INPUT_COUNT; index++)
    {
        if ((changed_bitmap & (uint16_t)(1UL << index)) != 0U)
        {
            elog_i("IO", "[IO] IN%u=%u active_low raw_bitmap=0x%03X",
                   (unsigned int)index,
                   (unsigned int)((current_bitmap >> index) & 0x01U),
                   (unsigned int)current_bitmap);
        }
    }

    *last_bitmap = current_bitmap;
}

static void MinSys_Set_All_Outputs(uint8_t active)
{
    uint8_t index;

    for (index = 0U; index < BSP_MIN_SYS_OUTPUT_COUNT; index++)
    {
        (void)MinSys_Set_Output(index, active);
    }

    for (index = 0U; index < BSP_MIN_SYS_RELAY_COUNT; index++)
    {
        (void)MinSys_Set_Relay(index, active);
    }
}

static void MinSys_Fram_Test(void)
{
    app_fram_test_result_t result;

    elog_i("FRAM",
           "[FRAM] test start addr7=0x%02X mem=0x%04X len=%u backup_restore=1",
           APP_FRAM_I2C_ADDRESS_7BIT,
           APP_FRAM_TEST_ADDRESS,
           APP_FRAM_TEST_LENGTH);

    result = App_Fram_Self_Test();
    if (result == APP_FRAM_TEST_OK)
    {
        elog_a("FRAM", "[FRAM] PASS write/read/verify/restore OK");
    }
    else
    {
        elog_e("FRAM", "[FRAM] FAIL step=%u hal_error=0x%08lX",
               (unsigned int)result,
               (unsigned long)App_Fram_Get_Last_Hal_Error());
    }
}
