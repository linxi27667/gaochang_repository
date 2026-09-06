/**
 * @file app_op_log.h
 * @brief Operation event logging interface used by lift safety and state changes.
 */
#ifndef __APP_OP_LOG_H__
#define __APP_OP_LOG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_OP_RESULT_OK = 0,
    APP_OP_RESULT_INTERRUPTED = 1,
    APP_OP_RESULT_FAILED = 2
} app_op_result_t;

void App_OpLog_Record(const char *event,
                      app_op_result_t result,
                      uint32_t duration_ms,
                      const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* __APP_OP_LOG_H__ */
