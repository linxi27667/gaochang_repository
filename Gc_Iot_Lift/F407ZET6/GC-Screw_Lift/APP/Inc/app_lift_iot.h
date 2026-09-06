#ifndef __APP_LIFT_IOT_H__
#define __APP_LIFT_IOT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIFT_IOT_DEVICE_ID          "gc_screw_lift_f407zet6"
#define LIFT_IOT_DEVICE_NAME        "GC-Screw-Lift-F407-01"
#define LIFT_IOT_DEVICE_MODEL       "GC-SCREW-F407"
#define LIFT_IOT_DEVICE_GROUP       "training-area-1"
#define LIFT_IOT_ADMIN_PASSWORD     "123456"
#define LIFT_IOT_PRODUCT_TYPE       "screw_lift"
#define LIFT_IOT_FIRMWARE_VERSION   "F407ZET6-SCREW-IOT-V1.0.1"

typedef enum
{
    LIFT_IOT_OK = 0,
    LIFT_IOT_PARAM_ERROR,
    LIFT_IOT_DENIED,
    LIFT_IOT_BUFFER_SMALL,
} lift_iot_result_t;

typedef struct
{
    uint8_t locked;
    uint8_t maintenance_due;
    uint8_t admin_mode;
    uint32_t run_count;
    uint32_t total_run_ms;
    uint32_t current_run_ms;
    uint32_t last_alarm_tick;
    uint32_t last_command_tick;
} lift_iot_status_t;

void App_LiftIot_Init(void);
void App_LiftIot_Poll(void);

uint8_t App_LiftIot_IsLocked(void);
lift_iot_result_t App_LiftIot_SetLocked(uint8_t locked, const char *source);
lift_iot_result_t App_LiftIot_EnterAdmin(const char *password, const char *account);
void App_LiftIot_ExitAdmin(void);
lift_iot_result_t App_LiftIot_ClearFault(const char *account);
lift_iot_result_t App_LiftIot_AdminJog(uint8_t column_index,
                                       uint8_t direction_up,
                                       uint32_t duration_ms,
                                       const char *account);
void App_LiftIot_MaintenanceDone(const char *account);

const lift_iot_status_t *App_LiftIot_GetStatus(void);
const char *App_LiftIot_StateName(void);

uint8_t App_LiftIot_PeekEventFlag(void);
uint8_t App_LiftIot_ConsumeEventFlag(void);
uint8_t App_LiftIot_IsInMotion(void);
lift_iot_result_t App_LiftIot_BuildHeightJson(char *buf,
                                             uint16_t size,
                                             uint32_t seq);

lift_iot_result_t App_LiftIot_BuildTelemetryJson(char *buf,
                                                 uint16_t size,
                                                 const char *type,
                                                 uint32_t seq,
                                                 const char *dtu_state,
                                                 int16_t csq);
lift_iot_result_t App_LiftIot_BuildStatusJson(char *buf,
                                              uint16_t size,
                                              const char *event,
                                              uint32_t seq,
                                              const char *dtu_state,
                                              int16_t csq);
lift_iot_result_t App_LiftIot_BuildCommandStatusJson(char *buf,
                                                     uint16_t size,
                                                     const char *event,
                                                     const char *cmd,
                                                     const char *msg_id,
                                                     const char *result,
                                                     uint32_t seq,
                                                     const char *dtu_state,
                                                     int16_t csq);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LIFT_IOT_H__ */
