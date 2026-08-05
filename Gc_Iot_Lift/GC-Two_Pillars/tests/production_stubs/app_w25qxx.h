#ifndef APP_W25QXX_H
#define APP_W25QXX_H
#include <stdint.h>

#define W25Q_OK 0U
#define W25Q_ERR 1U
#define W25Q_RISE_JOURNAL_ADDR 0x00008000U
#define W25Q_RISE_JOURNAL_SECTOR_SIZE 0x1000U
#define W25Q_RISE_JOURNAL_SECTOR_COUNT 16U
#define W25Q_MAINTENANCE_LEDGER_ADDR 0x00020000U
#define W25Q_MAINTENANCE_LEDGER_SECTOR_SIZE 0x1000U
#define W25Q_MAINTENANCE_LEDGER_SECTOR_COUNT 4U

typedef struct { uint8_t unused; } w25q_t;
typedef struct { uint32_t up_count; uint32_t up_count_main; } w25q_stats_t;
extern w25q_t W25Q_Flash;
extern w25q_stats_t g_stats;

uint8_t W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len);
uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *buf, uint16_t len);
uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr);
#endif
