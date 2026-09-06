#ifndef TEST_APP_W25QXX_H
#define TEST_APP_W25QXX_H

#include <stdint.h>
#include "app_product.h"

#define W25Q_OK  0U
#define W25Q_ERR 1U
#define W25Q_MAINTENANCE_THRESHOLD 5000U
#define W25Q_MAINTENANCE_COMMAND_CACHE_SIZE 16U

void App_W25Qxx_Stats_Inc_Up(lift_role_t role);
void App_W25Qxx_Stats_Inc_Down(lift_role_t role);
void App_W25Qxx_Stats_Inc_Lock(void);
void App_W25Qxx_Stats_Inc_Refill(void);
void App_W25Qxx_Stats_Inc_Estop(void);
void App_W25Qxx_Stats_Inc_PhotoAlarm(void);
void App_W25Qxx_Stats_Add_RunMs(uint32_t ms);

typedef struct
{
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t maintenance_due;
    uint32_t usage_epoch;
    uint32_t sequence;
    uint32_t command_hashes[W25Q_MAINTENANCE_COMMAND_CACHE_SIZE];
} w25q_maintenance_ledger_t;

uint8_t App_W25Qxx_Maintenance_Load(w25q_maintenance_ledger_t *ledger);
uint8_t App_W25Qxx_Maintenance_Increment(w25q_maintenance_ledger_t *ledger);
uint8_t App_W25Qxx_Maintenance_Done(const char *msg_id,
                                    w25q_maintenance_ledger_t *ledger);
uint8_t App_W25Qxx_Maintenance_ResetUsage(const char *msg_id,
                                          w25q_maintenance_ledger_t *ledger);

#endif
