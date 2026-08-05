#ifndef FAKE_APP_IO_MAP_H
#define FAKE_APP_IO_MAP_H

#include <stdint.h>

typedef enum {
    IO_IN_UP_BUTTON = 0,
    IO_IN_DOWN_BUTTON,
    IO_IN_LOCK_BUTTON,
    IO_IN_ESTOP,
    IO_IN_UPPER_LIMIT,
    IO_IN_LOWER_LIMIT,
    IO_IN_REFILL_BUTTON,
    IO_IN_PHOTOELECTRIC,
    IO_IN_MAX
} io_in_id_t;

typedef enum {
    IO_OUT_MOTOR = 0,
    IO_OUT_DROP_VALVE,
    IO_OUT_MAIN_AIR_VALVE,
    IO_OUT_MAX
} io_out_id_t;

uint8_t App_IO_Read(io_in_id_t id);
void App_IO_Write(io_out_id_t id, uint8_t value);
uint8_t App_IO_Read_Output(io_out_id_t id);
void App_IO_All_Off(void);
void App_IO_LogSnapshot(const char *reason);

#endif
