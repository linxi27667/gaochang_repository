#ifndef __APP_W25QXX_H__
#define __APP_W25QXX_H__

#include <stdint.h>
#include "bsp_w25qxx.h"
#include "main.h"

/* ============ FRAM 存储地址（FM24CL64B 8KB，双槽间距 256） ============ */
/* 注意：FRAM 共 8KB，勿占用 APP_FRAM_TEST_ADDRESS(0x1FF0) */
#define W25Q_SLOT_A_ADDR        0x0000U
#define W25Q_SLOT_B_ADDR        0x0100U

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
    uint16_t header;
    uint16_t tolerance_up;
    uint16_t tolerance_down;
    uint16_t stall_timeout_ms;
    uint16_t balance_wait_max_ms;
    uint16_t collision_debounce_ms;
    uint16_t secondary_descent_pulses;
    uint8_t  dual_column_mode;
    uint8_t  screw_lead_mm;
    uint16_t max_pulses;
    uint16_t crc16;
} w25q_config_t;

/* ============ 高度存储结构 ============ */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    int32_t  heights[2];
    uint32_t crc;
} w25q_height_t;

#define HEIGHT_SLOT_A_ADDR  0x0200U
#define HEIGHT_SLOT_B_ADDR  0x0300U
#define HEIGHT_FLASH_ADDR   HEIGHT_SLOT_A_ADDR
#define W25Q_HEIGHT_MAGIC   0x48494748U
#define W25Q_HEIGHT_VERSION 2U

extern w25q_t W25Q_Flash;
extern w25q_storage_t g_w25q_storage;
extern w25q_config_t g_config;

void App_W25Qxx_System_Init(void);
uint32_t App_W25Qxx_Get_JEDEC_ID(void);
uint8_t App_W25Qxx_Storage_Save(void);
void App_W25Qxx_Storage_Load(void);
void    App_W25Qxx_Height_Load(void);
uint8_t App_W25Qxx_Height_Save(void);
uint8_t App_W25Qxx_Height_Is_Loaded(void);

#define HEIGHT_MM(p)  ((int32_t)(p) * (int32_t)g_config.screw_lead_mm)

void App_W25Qxx_Height_Save_If_Needed(void);

#endif /* __APP_W25QXX_H__ */
