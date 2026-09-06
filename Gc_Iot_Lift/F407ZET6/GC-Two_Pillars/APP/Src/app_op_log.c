#include "app_op_log.h"
#include "app_w25qxx.h"
#include "stm32f4xx_hal.h"

#if OP_LOG_DEBUG == 1
#include "elog.h"
#endif

#include <string.h>

void App_OpLog_Record(op_type_t type, op_result_t result,
                      uint16_t duration_ms, const uint8_t *detail, uint8_t detail_len)
{
    w25q_op_log_entry_t entry = {0};
    entry.timestamp  = HAL_GetTick();
    entry.op_type    = (uint8_t)type;
    entry.op_result  = (uint8_t)result;
    entry.duration_ms= duration_ms;

    if (detail != NULL && detail_len > 0) {
        if (detail_len > 8) detail_len = 8;
        memcpy(entry.detail, detail, detail_len);
    }

    App_W25Qxx_OpLog_Append(&entry);

#if OP_LOG_DEBUG == 1
    elog_i("OPLOG", "[OPLOG] type=%d result=%d dur=%u cnt=%u",
           type, result, duration_ms, App_W25Qxx_OpLog_Count());
#endif
}

uint16_t App_OpLog_Count(void)
{
    return App_W25Qxx_OpLog_Count();
}

uint8_t App_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    return App_W25Qxx_OpLog_Read(index, out);
}

void App_OpLog_Clear(void)
{
    App_W25Qxx_OpLog_Clear();
#if OP_LOG_DEBUG == 1
    elog_i("OPLOG", "[OPLOG] Cleared");
#endif
}
