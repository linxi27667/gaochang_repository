#ifndef APP_MAINTENANCE_H
#define APP_MAINTENANCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MAINTENANCE_THRESHOLD 5000U

typedef struct
{
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint8_t maintenance_due;
    uint32_t usage_epoch;
} app_maintenance_status_t;

/* Initializes the append-only maintenance ledger after W25Q is ready. */
void App_Maintenance_Init(void);
void App_Maintenance_GetStatus(app_maintenance_status_t *out);

/* Each function appends exactly one committed ledger record on success. */
uint8_t App_Maintenance_RecordRise(void);
uint8_t App_Maintenance_Done(const char *msg_id);
uint8_t App_Maintenance_ResetUsage(const char *msg_id);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAINTENANCE_H */
