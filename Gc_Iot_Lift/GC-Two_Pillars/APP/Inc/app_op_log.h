#ifndef __APP_OP_LOG_H__
#define __APP_OP_LOG_H__

#include <stdint.h>
#include "app_w25qxx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 操作类型枚举（20 种，按 2026-07-02 计划第 6.3 节） ============ */
typedef enum {
    OP_UP_START              = 0,   /* 上升开始 */
    OP_UP_STOP_RELEASE       = 1,   /* 上升停止（按钮释放） */
    OP_UP_STOP_LIMIT         = 2,   /* 上升停止（上限位触发） */
    OP_DOWN_START            = 3,   /* 下降开始 */
    OP_DOWN_STOP_RELEASE     = 4,   /* 下降停止（按钮释放） */
    OP_DOWN_STOP_LIMIT       = 5,   /* 下降停止（上限位/锁定强制下降） */
    OP_LOCK_START            = 6,   /* 锁定开始 */
    OP_LOCK_STOP             = 7,   /* 锁定停止 */
    OP_REFILL_START          = 8,   /* 补油开始 */
    OP_REFILL_STOP           = 9,   /* 补油停止 */
    OP_ESTOP                 = 10,  /* 急停触发 */
    OP_PHOTO_ALARM           = 11,  /* 光电报警 */
    OP_ROTARY_SWITCH         = 12,  /* 旋转开关切换 */
    OP_REMOTE_CLEAR_ALARM    = 13,  /* 远程解除报警 */
    OP_REMOTE_LOCK           = 14,  /* 远程锁定 */
    OP_REMOTE_UNLOCK         = 15,  /* 远程解锁 */
    OP_CONFIG_CHANGE         = 16,  /* 配置变更 */
    OP_PRODUCT_TYPE_CHANGE   = 17,  /* 产品类型变更 */
    OP_POWER_ON              = 18,  /* 开机 */
    OP_POWER_OFF             = 19,  /* 关机 */
    OP_TYPE_MAX
} op_type_t;

/* ============ 操作结果 ============ */
typedef enum {
    OP_RESULT_OK         = 0,   /* 正常完成 */
    OP_RESULT_INTERRUPTED= 1,   /* 被中断（如急停、报警） */
    OP_RESULT_FAILED     = 2,   /* 失败 */
} op_result_t;

/* ============ 接口 ============ */

/**
 * @brief  记录一条操作日志
 * @param  type 操作类型
 * @param  result 操作结果
 * @param  duration_ms 动作持续时长（毫秒）
 * @param  detail 详情数据（如角色 0=main 1=sub），可为 NULL
 * @param  detail_len 详情长度（最多 8 字节）
 */
void App_OpLog_Record(op_type_t type, op_result_t result,
                      uint16_t duration_ms, const uint8_t *detail, uint8_t detail_len);

/**
 * @brief  获取当前日志条数
 */
uint16_t App_OpLog_Count(void);

/**
 * @brief  读取指定索引的日志
 */
uint8_t App_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out);

/**
 * @brief  清空所有日志
 */
void App_OpLog_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_OP_LOG_H__ */
