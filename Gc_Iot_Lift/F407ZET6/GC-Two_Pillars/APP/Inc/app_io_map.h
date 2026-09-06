#ifndef __APP_IO_MAP_H__
#define __APP_IO_MAP_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 杈撳叆 ID锛堥€昏緫鍚嶏紝涓庣墿鐞嗗紩鑴氳В鑰︼級 ============ */
typedef enum {
    IO_IN_UP_BUTTON     = 0,   /* 涓婂崌鎸夐挳 */
    IO_IN_DOWN_BUTTON   = 1,   /* 涓嬮檷鎸夐挳 */
    IO_IN_LOCK_BUTTON   = 2,   /* 閿佸畾鎸夐挳 */
    IO_IN_ESTOP         = 3,   /* 鎬ュ仠 */
    IO_IN_UPPER_LIMIT   = 4,   /* 涓婇檺浣嶏紙澶у壀鐢辨棆杞紑鍏冲垏鎹富/瀛愭満锛?*/
    IO_IN_LOWER_LIMIT   = 5,   /* 涓嬮檺浣嶏紙浠呰秴钖勫皬鍓級 */
    IO_IN_REFILL_BUTTON = 6,   /* 琛ユ补鎸夐挳 */
    IO_IN_PHOTOELECTRIC = 7,   /* 鍏夌數寮€鍏?*/
    IO_IN_ROTARY_SWITCH = 8,   /* 鏃嬭浆寮€鍏筹紙浠呭ぇ鍓級 */
    IO_IN_MAX
} io_in_id_t;

/* ============ 杈撳嚭 ID ============ */
typedef enum {
    IO_OUT_MOTOR            = 0,   /* Motor relay */
    IO_OUT_DROP_VALVE       = 1,   /* Drop valve: double-post PD8, large-scissor PF9 */
    IO_OUT_MAIN_AIR_VALVE   = 2,   /* Main air valve / double-post electromagnet */
    IO_OUT_MAIN_WORK_VALVE  = 3,   /* 涓绘満宸ヤ綔闃€ (PD9) */
    IO_OUT_SUB_AIR_VALVE    = 4,   /* 瀛愭満姘旈榾 (PD10) */
    IO_OUT_SUB_WORK_VALVE   = 5,   /* 瀛愭満宸ヤ綔闃€ (PD11) */
    IO_OUT_MAX
} io_out_id_t;

#define IO_OUT_ELECTROMAGNET IO_OUT_MAIN_AIR_VALVE

/* ============ 鎺ュ彛 ============ */

/**
 * @brief  鍒濆鍖?I/O 鏄犲皠琛紙鎸変骇鍝佺被鍨嬶級
 * @param  type 浜у搧绫诲瀷
 */
void App_IO_Map_Init(product_type_t type);

/**
 * @brief  璇诲彇杈撳叆锛堝綊涓€鍖栵紝鍚幓鎶栵級
 * @param  id 杈撳叆 ID
 * @return 0=鏈Е鍙?鏈寜涓嬶紝1=瑙﹀彂/鎸変笅
 */
uint8_t App_IO_Read(io_in_id_t id);

/**
 * @brief  璇诲彇杈撳叆鍘熷鍊硷紙涓嶅惈鍘绘姈锛岀敤浜庤皟璇曪級
 * @param  id 杈撳叆 ID
 * @return 0/1
 */
uint8_t App_IO_Read_Raw(io_in_id_t id);

void App_IO_LogSnapshot(const char *reason);

/**
 * @brief  鍐欒緭鍑? * @param  id 杈撳嚭 ID
 * @param  value 0=鏂紑, 1=鎺ラ€? */
void App_IO_Write(io_out_id_t id, uint8_t value);

/**
 * @brief  璇诲彇褰撳墠杈撳嚭鐘舵€? * @param  id 杈撳嚭 ID
 * @return 0=鏂紑, 1=鎺ラ€? */
uint8_t App_IO_Read_Output(io_out_id_t id);

/**
 * @brief  鏂紑鎵€鏈夎緭鍑猴紙瀹夊叏鎬ュ仠鐢級
 */
void App_IO_All_Off(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IO_MAP_H__ */
