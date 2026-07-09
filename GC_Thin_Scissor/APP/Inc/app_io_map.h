#ifndef __APP_IO_MAP_H__
#define __APP_IO_MAP_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IO_IN_UP_BUTTON     = 0,  /* PG15 */
    IO_IN_DOWN_BUTTON   = 1,  /* PE0 */
    IO_IN_LOCK_BUTTON   = 2,  /* PE1 */
    IO_IN_ESTOP         = 3,  /* PE2 */
    IO_IN_UPPER_LIMIT   = 4,  /* PE5 */
    IO_IN_LOWER_LIMIT   = 5,  /* PE6 */
    IO_IN_REFILL_BUTTON = 6,  /* PE3 */
    IO_IN_PHOTOELECTRIC = 7,  /* PE8 */
    IO_IN_MAX,

    /* Compatibility names for shared/legacy code paths. These pins are not
     * mounted on the thin scissor board, and App_IO_Read() returns 0 for them. */
    IO_IN_ROTARY_SWITCH    = IO_IN_MAX,
    IO_IN_SUB_UPPER_LIMIT  = IO_IN_MAX + 1
} io_in_id_t;

typedef enum {
    IO_OUT_MOTOR          = 0,  /* PF8 */
    IO_OUT_DROP_VALVE     = 1,  /* PF9 */
    IO_OUT_MAIN_AIR_VALVE = 2,  /* PD8 */
    IO_OUT_MAX,

    /* Compatibility names for shared/legacy code paths. These outputs are not
     * mounted on the thin scissor board, and App_IO_Write() ignores them. */
    IO_OUT_MAIN_WORK_VALVE = IO_OUT_MAX,
    IO_OUT_SUB_AIR_VALVE   = IO_OUT_MAX + 1,
    IO_OUT_SUB_WORK_VALVE  = IO_OUT_MAX + 2
} io_out_id_t;

void App_IO_Map_Init(product_type_t type);

uint8_t App_IO_Read(io_in_id_t id);
uint8_t App_IO_Read_Raw(io_in_id_t id);

void App_IO_PollInputs(void);
void App_IO_LogSnapshot(const char *reason);

void App_IO_Write(io_out_id_t id, uint8_t value);
uint8_t App_IO_Read_Output(io_out_id_t id);
void App_IO_All_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IO_MAP_H__ */
