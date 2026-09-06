#ifndef FAKE_APP_W25QXX_H
#define FAKE_APP_W25QXX_H

#include <stdint.h>
#include "app_product.h"

typedef struct {
    product_type_t product_type;
    uint32_t motor_to_valve_delay_ms;
    uint32_t motor_hold_ms;
} app_config_t;

extern app_config_t g_config;

void App_W25Qxx_Stats_Inc_Up(lift_role_t role);
void App_W25Qxx_Stats_Inc_Down(lift_role_t role);
void App_W25Qxx_Stats_Inc_Lock(void);
void App_W25Qxx_Stats_Inc_Refill(void);
void App_W25Qxx_Stats_Inc_Estop(void);
void App_W25Qxx_Stats_Inc_PhotoAlarm(void);

#endif
