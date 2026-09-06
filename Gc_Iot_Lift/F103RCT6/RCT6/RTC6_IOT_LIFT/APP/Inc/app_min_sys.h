#ifndef APP_MIN_SYS_H
#define APP_MIN_SYS_H

#include <stdint.h>
#include "bsp_min_sys.h"

uint8_t App_MinSys_Init(void);
uint8_t MinSys_Sample(void);
void MinSys_Get_Snapshot(bsp_min_sys_snapshot_t *snapshot);
void MinSys_Safe_Off(void);
uint8_t MinSys_Set_Output(uint8_t index, uint8_t active);
uint8_t MinSys_Set_Relay(uint8_t index, uint8_t active);
void MinSys_Set_Run_Led(uint8_t active);
void MinSys_Toggle_Run_Led(void);
uint8_t MinSys_Is_Adc_Ready(void);

#endif
