#include "app_op_log.h"
#include "elog.h"

void App_OpLog_Record(const char *event,
                      app_op_result_t result,
                      uint32_t duration_ms,
                      const char *detail)
{
    if (event == 0) {
        event = "unknown";
    }
    if (detail == 0) {
        detail = "";
    }

    elog_e("OP",
           "[OP] event=%s result=%u duration=%lu detail=%s",
           event,
           (unsigned int)result,
           (unsigned long)duration_ms,
           detail);
}
