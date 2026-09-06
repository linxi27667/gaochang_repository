#ifndef RISE_STATS_FAKE_APP_OP_LOG_H
#define RISE_STATS_FAKE_APP_OP_LOG_H

#include <stdint.h>
#include "app_w25qxx.h"

uint16_t App_OpLog_Count(void);
uint8_t App_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out);
void App_OpLog_Clear(void);

#endif
