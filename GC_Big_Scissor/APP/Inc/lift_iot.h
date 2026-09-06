#ifndef __LIFT_IOT_H__
#define __LIFT_IOT_H__

#include <stdint.h>
#include "lift_core.h"   /* 为 lift_state_t */
#include "app_product.h"     /* 为 lift_role_t / g_product_type / g_current_role */

#ifdef __cplusplus
extern "C" {
#endif

#define LIFT_IOT_DEVICE_ID          "gaochang_lift_f407zet6"
#define LIFT_IOT_DEVICE_NAME        "Gaochang-LargeScissor-F407-01"
#define LIFT_IOT_DEVICE_MODEL       "GC-F407-LSCISSOR"
#define LIFT_IOT_DEVICE_GROUP       "training-area-1"
#define LIFT_IOT_ADMIN_PASSWORD     "123456"

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

/* ============ 快照结构（推荐使用：临界区最短） ============ */

/**
 * @brief 举升机控制状态快照（不含 DTU 关心的字段）
 */
typedef struct {
    lift_state_t state;          /* 当前状态 */
    uint8_t      remote_locked;  /* 远程锁定标志 */
} lift_state_snapshot_t;

/**
 * @brief IoT 状态快照（仅与 telemetry / 上报相关的字段）
 */
typedef struct {
    uint8_t  locked;
    uint8_t  maintenance_due;
    uint8_t  admin_mode;
    uint32_t run_count;
    uint32_t total_run_ms;
    uint32_t current_run_ms;
    uint8_t  event_pending;
} lift_iot_snapshot_t;

/**
 * @brief 一次性读两个快照（按顺序获取两把锁，避免死锁）
 *        供 telemetry JSON 构造使用，调用后离线格式化
 */
void LiftIot_Snapshot(lift_state_snapshot_t *out_state,
                      lift_iot_snapshot_t  *out_iot);

void LiftIot_Init(void);
void LiftIot_Poll(void);

uint8_t LiftIot_IsLocked(void);
lift_iot_result_t LiftIot_SetLocked(uint8_t locked, const char *source);
lift_iot_result_t LiftIot_EnterAdmin(const char *password, const char *account);
void LiftIot_ExitAdmin(void);
lift_iot_result_t LiftIot_ClearFault(const char *account);
lift_iot_result_t LiftIot_AdminJog(uint8_t column_index,
                                       uint8_t direction_up,
                                       uint32_t duration_ms,
                                       const char *account);
lift_iot_result_t LiftIot_MaintenanceDone(const char *account, const char *msg_id);
lift_iot_result_t LiftIot_ResetUsage(const char *account, const char *msg_id);

const lift_iot_status_t *LiftIot_GetStatus(void);
const char *LiftIot_StateName(void);

uint8_t LiftIot_PeekEventFlag(void);
uint8_t LiftIot_ConsumeEventFlag(void);
uint8_t LiftIot_IsInMotion(void);
lift_iot_result_t LiftIot_BuildHeightJson(char *buf,
                                             uint16_t size,
                                             uint32_t seq);

lift_iot_result_t LiftIot_BuildTelemetryJson(char *buf,
                                                 uint16_t size,
                                                 const char *type,
                                                 uint32_t seq,
                                                 const char *dtu_state,
                                                 int16_t csq);
lift_iot_result_t LiftIot_BuildStatusJson(char *buf,
                                              uint16_t size,
                                              const char *event,
                                              uint32_t seq,
                                              const char *dtu_state,
                                              int16_t csq);
lift_iot_result_t LiftIot_BuildCommandStatusJson(char *buf,
                                                     uint16_t size,
                                                     const char *event,
                                                     const char *cmd,
                                                     const char *msg_id,
                                                     const char *result,
                                                     uint32_t seq,
                                                     const char *dtu_state,
                                                     int16_t csq);

/* ============ 大剪专用接口（新增） ============ */

/**
 * @brief  构造大剪专用 telemetry JSON
 *         由 LiftIot_BuildTelemetryJson 内部按 g_product_type 分发调用
 */
lift_iot_result_t LiftIot_BuildLargeScissorTelemetry(char *buf,
                                                         uint16_t size,
                                                         uint32_t seq,
                                                         const char *dtu_state,
                                                         int16_t csq);

/**
 * @brief  构造操作日志批量上传 JSON
 * @param  start_index 起始日志索引
 * @param  count 本次最多打包的条数（实际可能少于）
 */
lift_iot_result_t LiftIot_BuildOpLogJson(char *buf,
                                             uint16_t size,
                                             uint16_t start_index,
                                             uint16_t count,
                                             uint32_t seq);

/* ============ 远程命令处理（新增入口，与旧 API 并存） ============ */

lift_iot_result_t LiftIot_HandleClearAlarm(const char *account);
lift_iot_result_t LiftIot_HandleRemoteLock(uint8_t locked, const char *account);
lift_iot_result_t LiftIot_HandleEnterMaintenance(const char *account);
lift_iot_result_t LiftIot_HandleExitMaintenance(const char *account);

/* ============ 事件上报钩子（由 LiftCore 状态变化触发） ============ */

void LiftIot_NotifyPhotoAlarm(void);
void LiftIot_NotifyEstop(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIFT_IOT_H__ */
