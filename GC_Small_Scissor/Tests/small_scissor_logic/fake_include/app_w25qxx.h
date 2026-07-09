#ifndef TEST_APP_W25QXX_H
#define TEST_APP_W25QXX_H

#include <stdint.h>
#include "app_product.h"

void App_W25Qxx_Stats_Inc_Up(lift_role_t role);
void App_W25Qxx_Stats_Inc_Down(lift_role_t role);
void App_W25Qxx_Stats_Inc_Lock(void);
void App_W25Qxx_Stats_Inc_Refill(void);
void App_W25Qxx_Stats_Inc_Estop(void);
void App_W25Qxx_Stats_Inc_PhotoAlarm(void);

#endif
