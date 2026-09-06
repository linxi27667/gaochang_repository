#ifndef RISE_STATS_FAKE_RUNTIME_H
#define RISE_STATS_FAKE_RUNTIME_H

#include <stdint.h>

#include "app_io_map.h"
#include "lift_core.h"

void fake_reset(void);
void fake_reboot(void);
void fake_advance_ms(uint32_t ms);
void fake_set_state(lift_state_t state);
void fake_set_input(io_in_id_t id, uint8_t active);
uint8_t fake_output(io_out_id_t id);
int fake_map_chip_uid(void);
void fake_corrupt_flash(uint32_t addr);

#endif
