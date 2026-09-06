/**
 * @file app_op_log.c
 * @brief Compact operation log builder and elog output for key lift events.
 */
#include "app_op_log.h"

#include "app_w25qxx.h"
#include "elog.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

static uint8_t App_OpLog_EventCode(const char *event)
{
    uint8_t code = 0U;

    if (event == NULL) {
        return 0U;
    }

    while (*event != '\0') {
        code = (uint8_t)(code * 33U + (uint8_t)(*event));
        event++;
    }

    return code;
}

void App_OpLog_Record(const char *event,
                      app_op_result_t result,
                      uint32_t duration_ms,
                      const char *detail)
{
    w25q_op_log_entry_t entry;

    if (event == NULL) {
        event = "unknown";
    }

    if (detail == NULL) {
        detail = "";
    }

    memset(&entry, 0, sizeof(entry));
    entry.timestamp = HAL_GetTick();
    entry.op_type = App_OpLog_EventCode(event);
    entry.op_result = (uint8_t)result;
    entry.duration_ms = (duration_ms > 0xFFFFU) ? 0xFFFFU : (uint16_t)duration_ms;
    (void)snprintf((char *)entry.detail, sizeof(entry.detail), "%.7s", detail);

    (void)App_W25Qxx_OpLog_Append(&entry);

    elog_i("OPLOG",
           "[OPLOG] event=%s result=%u dur=%lu detail=%s cnt=%u",
           event,
           (unsigned int)result,
           (unsigned long)duration_ms,
           detail,
           (unsigned int)App_W25Qxx_OpLog_Count());
}
