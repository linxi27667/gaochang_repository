#ifndef __APP_W25QXX_H__
#define __APP_W25QXX_H__

#include <stdint.h>
#include "bsp_w25qxx.h"
#include "main.h"

/* ============ W25Q 存储地址（双扇区轮转） ============ */

#define W25Q_SLOT_A_ADDR        0x00000000U
#define W25Q_SLOT_B_ADDR        0x00001000U

/* ============ 持久化数据结构 ============ */

typedef struct
{
    uint32_t magic;
    uint32_t debug_counter;
    uint32_t crc;
} w25q_storage_t;

#define W25Q_STORAGE_MAGIC      0x53544F52U

#define W25Q_STORAGE_SIZE       sizeof(w25q_storage_t)

/* ============ 可持久化配置参数 ============ */

typedef struct {
    uint16_t header;                       /* 0xA5A5 */
    uint16_t tolerance_up;                 /* 上升允差（脉冲数） */
    uint16_t tolerance_down;               /* 下降允差（脉冲数） */
    uint16_t stall_timeout_ms;             /* 堵转判断时间 */
    uint16_t balance_wait_max_ms;          /* 平衡等待超时 */
    uint16_t collision_debounce_ms;        /* 防碰去抖时间 */
    uint16_t secondary_descent_pulses;     /* 二次下降高度（脉冲数） */
    uint8_t  dual_column_mode;             /* 0=独立 1=双柱联动 */
    uint8_t  screw_lead_mm;                /* 丝杆导程 */
    uint16_t max_pulses;                   /* 上限位脉冲数（到顶停机） */
    uint16_t crc16;                        /* CRC16 */
} w25q_config_t;

/* ============ 高度存储结构 ============ */

typedef struct {
    uint32_t magic;         /* 0x48494748 = "HIGH" */
    int32_t  heights[2];    /* 两个立柱脉冲计数 */
    uint32_t crc;           /* CRC32 */
} w25q_height_t;

/* 高度存储地址：Sector 2，避开 Slot A(0x0000) 和 Slot B(0x1000) */
#define HEIGHT_FLASH_ADDR   0x00002000U

/* ============ 全局对象 ============ */

extern w25q_t W25Q_Flash;

extern w25q_storage_t g_w25q_storage;

extern w25q_config_t g_config;

/* ============ 系统初始化 ============ */
void App_W25Qxx_System_Init(void);

/* ============ 对外接口 ============ */

uint32_t App_W25Qxx_Get_JEDEC_ID(void);

uint8_t App_W25Qxx_Storage_Save(void);

void App_W25Qxx_Storage_Load(void);

void    App_W25Qxx_Height_Load(void);

uint8_t App_W25Qxx_Height_Save(void);

#endif /* __APP_W25QXX_H__ */
