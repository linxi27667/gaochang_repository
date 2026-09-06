#ifndef FAKE_RUNTIME_H
#define FAKE_RUNTIME_H

#include <stdint.h>
#include "app_io_map.h"

void fake_reset(void);
void fake_advance_ms(uint32_t ms);
void fake_set_input(io_in_id_t id, uint8_t active);
uint8_t fake_output(io_out_id_t id);
int fake_log_contains(const char *needle);

#endif
