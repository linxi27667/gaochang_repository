#ifndef FIFO_H
#define FIFO_H

#include <stdint.h>
#include <stdbool.h>

#ifndef FIFO_BUFFER_SIZE
#define FIFO_BUFFER_SIZE 256
#endif

typedef struct {
    uint8_t  buffer[FIFO_BUFFER_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} FIFO_t;

/* ==================== Manual mode (copy-based, uses count) ==================== */

void     fifo_init(FIFO_t *fifo);
bool     fifo_is_empty(const FIFO_t *fifo);
bool     fifo_is_full(const FIFO_t *fifo);
uint16_t fifo_get_count(const FIFO_t *fifo);
uint16_t fifo_get_remaining(const FIFO_t *fifo);
bool     fifo_write_byte(FIFO_t *fifo, uint8_t data);
bool     fifo_read_byte(FIFO_t *fifo, uint8_t *data);
uint16_t fifo_write_bytes(FIFO_t *fifo, const uint8_t *data, uint16_t len);
uint16_t fifo_read_bytes(FIFO_t *fifo, uint8_t *data, uint16_t len);
void     fifo_clear(FIFO_t *fifo);

/* ==================== DMA mode (no copy, uses head/tail arithmetic) ==================== */

void     fifo_dma_notify(FIFO_t *fifo, uint16_t new_bytes);
uint16_t fifo_dma_read(FIFO_t *fifo, uint8_t *buf, uint16_t max);
uint16_t fifo_dma_available(FIFO_t *fifo);
void     fifo_dma_flush(FIFO_t *fifo);

#endif
