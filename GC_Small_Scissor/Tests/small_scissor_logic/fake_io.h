#ifndef TEST_FAKE_IO_H
#define TEST_FAKE_IO_H

#include <stdint.h>
#include "app_io_map.h"

void fake_io_reset(void);
void fake_io_set_raw(io_in_id_t id, uint8_t raw);
uint8_t fake_io_get_out(io_out_id_t id);

#endif
