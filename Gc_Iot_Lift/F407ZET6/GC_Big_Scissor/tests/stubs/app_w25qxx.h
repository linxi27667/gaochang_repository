#ifndef APP_W25QXX_H
#define APP_W25QXX_H

#include <stdint.h>

#define W25Q_OK  0U
#define W25Q_ERR 1U

#define W25Q_MAINTENANCE_ADDR         0x00020000U
#define W25Q_MAINTENANCE_SECTOR_SIZE  0x00001000U
#define W25Q_MAINTENANCE_SECTOR_COUNT 4U
#define W25Q_RISE_QUEUE_ADDR          0x00008000U
#define W25Q_RISE_QUEUE_SECTOR_SIZE   0x00001000U
#define W25Q_RISE_QUEUE_SECTOR_COUNT  16U

typedef struct { uint8_t unused; } w25q_t;

typedef struct
{
    uint32_t up_count;
    uint32_t up_count_main;
    uint32_t up_count_sub;
} w25q_stats_t;

typedef struct
{
    uint32_t sequence;
    uint32_t up_total;
    uint32_t up_main;
    uint32_t up_sub;
    uint32_t usage_epoch;
    int role;
    uint8_t from_flash;
} w25q_rise_event_t;

extern w25q_t W25Q_Flash;
extern w25q_stats_t g_stats;

uint8_t W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len);
uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr);
uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len);
uint8_t W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len);

uint8_t App_W25Qxx_RiseQueue_PersistPending(void);
uint8_t App_W25Qxx_RiseQueue_Peek(w25q_rise_event_t *out);

#endif /* APP_W25QXX_H */
