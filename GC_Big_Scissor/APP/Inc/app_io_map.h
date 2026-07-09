#ifndef __APP_IO_MAP_H__
#define __APP_IO_MAP_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 输入 ID（逻辑名，与物理引脚解耦） ============ */
typedef enum {
    IO_IN_UP_BUTTON     = 0,   /* 上升按钮 */
    IO_IN_DOWN_BUTTON   = 1,   /* 下降按钮 */
    IO_IN_LOCK_BUTTON   = 2,   /* 锁定按钮 */
    IO_IN_ESTOP         = 3,   /* 急停 */
    IO_IN_UPPER_LIMIT   = 4,   /* 上限位（大剪由旋转开关切换主/子机） */
    IO_IN_LOWER_LIMIT   = 5,   /* 下限位（仅超薄小剪） */
    IO_IN_REFILL_BUTTON = 6,   /* 补油按钮 */
    IO_IN_PHOTOELECTRIC = 7,   /* 光电开关 */
    IO_IN_ROTARY_SWITCH = 8,   /* 旋转开关（仅大剪） */
    IO_IN_SUB_UPPER_LIMIT = 9, /* 子机上限位（仅大剪子机） */
    IO_IN_SUB_LOWER_LIMIT = 10, /* 子机下限位（仅大剪子机） */
    IO_IN_MAX
} io_in_id_t;

/* ============ 输出 ID ============ */
typedef enum {
    IO_OUT_MOTOR            = 0,   /* 电机继电器 (PF8) */
    IO_OUT_DROP_VALVE       = 1,   /* 下降阀 (PF9) */
    IO_OUT_MAIN_AIR_VALVE   = 2,   /* 主机气阀 (PD8) */
    IO_OUT_MAIN_WORK_VALVE  = 3,   /* 主机工作阀 (PD9) */
    IO_OUT_SUB_AIR_VALVE    = 4,   /* 子机气阀 (PD10) */
    IO_OUT_SUB_WORK_VALVE   = 5,   /* 子机工作阀 (PD11) */
    IO_OUT_MAX
} io_out_id_t;

/* ============ 接口 ============ */

/**
 * @brief  初始化 I/O 映射表（按产品类型）
 * @param  type 产品类型
 */
void App_IO_Map_Init(product_type_t type);

/**
 * @brief  读取输入（归一化，含去抖）
 * @param  id 输入 ID
 * @return 0=未触发/未按下，1=触发/按下
 */
uint8_t App_IO_Read(io_in_id_t id);

/**
 * @brief  读取输入原始值（不含去抖，用于调试）
 * @param  id 输入 ID
 * @return 0/1
 */
uint8_t App_IO_Read_Raw(io_in_id_t id);

/**
 * @brief  Refresh debounce state for every input once per control cycle.
 */
void App_IO_PollInputs(void);

/**
 * @brief  Print one diagnostic snapshot of all logical inputs.
 * @param  reason Short reason string printed in the log.
 */
void App_IO_LogSnapshot(const char *reason);

/**
 * @brief  写输出
 * @param  id 输出 ID
 * @param  value 0=断开, 1=接通
 */
void App_IO_Write(io_out_id_t id, uint8_t value);

/**
 * @brief  读取当前输出状态
 * @param  id 输出 ID
 * @return 0=断开, 1=接通
 */
uint8_t App_IO_Read_Output(io_out_id_t id);

/**
 * @brief  断开所有输出（安全急停用）
 */
void App_IO_All_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IO_MAP_H__ */
