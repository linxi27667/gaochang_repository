#ifndef FAKE_APP_OP_LOG_H
#define FAKE_APP_OP_LOG_H

#include <stdint.h>

typedef enum {
    OP_POWER_ON = 0,
    OP_UP_START,
    OP_UP_STOP_RELEASE,
    OP_UP_STOP_LIMIT,
    OP_DOWN_START,
    OP_DOWN_STOP_RELEASE,
    OP_DOWN_STOP_LIMIT,
    OP_LOCK_START,
    OP_LOCK_STOP,
    OP_REFILL_START,
    OP_REFILL_STOP,
    OP_ESTOP,
    OP_PHOTO_ALARM,
    OP_REMOTE_CLEAR_ALARM,
    OP_REMOTE_LOCK,
    OP_REMOTE_UNLOCK
} op_type_t;

typedef enum {
    OP_RESULT_OK = 0,
    OP_RESULT_INTERRUPTED = 1
} op_result_t;

int App_OpLog_Record(op_type_t op, op_result_t result, uint16_t duration_s,
                     const void *extra, uint8_t extra_len);
const char *fake_op_log_last(void);

#endif
