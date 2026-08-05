#ifndef __APP_RISE_COUNTER_H__
#define __APP_RISE_COUNTER_H__

#include <stdint.h>
#include "lift_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Persistent, time-based usage counters for the double-post lift. */
typedef struct
{
    uint32_t rise_total_ms;
    uint32_t rise_count;
    uint32_t rise_remainder_ms;
    uint32_t pending_count;
    uint32_t upload_seq;
} app_rise_counter_snapshot_t;

#define APP_MAINTENANCE_THRESHOLD 5000U

typedef struct
{
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t usage_epoch;
    uint32_t maintenance_revision;
    uint32_t last_command_hash;
    uint8_t last_command_type;
    uint8_t maintenance_due;
} app_maintenance_snapshot_t;

/* A contiguous, not-yet-acknowledged upload range. */
typedef struct
{
    uint32_t seq;
    uint32_t delta;
    uint32_t total_count;
    uint32_t total_rise_ms;
    uint32_t remainder_ms;
    uint32_t usage_epoch;
} app_rise_counter_upload_t;

/* Call after App_W25Qxx_System_Init(), before motion tasks are created. */
void App_RiseCounter_Init(void);

/* Creates the low-priority W25Q journal worker. Safe to call more than once. */
void App_RiseCounter_Task_Create(void);

/* Called by the lift control task after LiftCore_Poll(). This function never
 * accesses W25Q or waits for a worker task. */
void App_RiseCounter_Poll(lift_state_t state, uint32_t now);

void App_RiseCounter_GetSnapshot(app_rise_counter_snapshot_t *out);
void App_RiseCounter_GetMaintenanceSnapshot(app_maintenance_snapshot_t *out);

/* These calls return success only after the changed maintenance state has
 * been committed and verified by the W25Q journal worker. */
uint8_t App_RiseCounter_MaintenanceDone(const char *msg_id);
uint8_t App_RiseCounter_ResetUsage(const char *msg_id);

/* Returns the oldest contiguous pending range. `seq` identifies its first
 * count and `delta` identifies the length of that range. */
uint8_t App_RiseCounter_GetPendingUpload(app_rise_counter_upload_t *out);

/* Confirm exactly the range returned by App_RiseCounter_GetPendingUpload().
 * Call only after the upload has been accepted by the transport/application. */
uint8_t App_RiseCounter_MarkUploadSent(uint32_t seq, uint32_t delta);

#ifdef __cplusplus
}
#endif

#endif /* __APP_RISE_COUNTER_H__ */
