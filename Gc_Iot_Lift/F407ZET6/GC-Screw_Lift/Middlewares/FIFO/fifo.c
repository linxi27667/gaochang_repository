#include "fifo.h"
#include <string.h>

/* ==================== Manual mode ==================== */

void fifo_init(FIFO_t *fifo)
{
    if (fifo == NULL) return;
    fifo->head  = 0;
    fifo->tail  = 0;
    fifo->count = 0;
    memset(fifo->buffer, 0, FIFO_BUFFER_SIZE);
}

bool fifo_is_empty(const FIFO_t *fifo)
{
    if (fifo == NULL) return true;
    return (fifo->count == 0);
}

bool fifo_is_full(const FIFO_t *fifo)
{
    if (fifo == NULL) return false;
    return (fifo->count == FIFO_BUFFER_SIZE);
}

uint16_t fifo_get_count(const FIFO_t *fifo)
{
    if (fifo == NULL) return 0;
    return fifo->count;
}

uint16_t fifo_get_remaining(const FIFO_t *fifo)
{
    if (fifo == NULL) return 0;
    return (FIFO_BUFFER_SIZE - fifo->count);
}

bool fifo_write_byte(FIFO_t *fifo, uint8_t data)
{
    if (fifo == NULL || fifo_is_full(fifo)) return false;
    fifo->buffer[fifo->head] = data;
    fifo->head = (fifo->head + 1) % FIFO_BUFFER_SIZE;
    fifo->count++;
    return true;
}

bool fifo_read_byte(FIFO_t *fifo, uint8_t *data)
{
    if (fifo == NULL || data == NULL || fifo_is_empty(fifo)) return false;
    *data = fifo->buffer[fifo->tail];
    fifo->tail = (fifo->tail + 1) % FIFO_BUFFER_SIZE;
    fifo->count--;
    return true;
}

uint16_t fifo_write_bytes(FIFO_t *fifo, const uint8_t *data, uint16_t len)
{
    if (fifo == NULL || data == NULL || len == 0) return 0;

    uint16_t remaining = fifo_get_remaining(fifo);
    uint16_t write_len = (len > remaining) ? remaining : len;
    if (write_len == 0) return 0;

    if (fifo->head + write_len <= FIFO_BUFFER_SIZE) {
        memcpy(&fifo->buffer[fifo->head], data, write_len);
        fifo->head += write_len;
    } else {
        uint16_t first_part = FIFO_BUFFER_SIZE - fifo->head;
        memcpy(&fifo->buffer[fifo->head], data, first_part);
        uint16_t second_part = write_len - first_part;
        memcpy(fifo->buffer, &data[first_part], second_part);
        fifo->head = second_part;
    }

    fifo->count += write_len;
    return write_len;
}

uint16_t fifo_read_bytes(FIFO_t *fifo, uint8_t *data, uint16_t len)
{
    if (fifo == NULL || data == NULL || len == 0) return 0;

    uint16_t available = fifo_get_count(fifo);
    uint16_t read_len = (len > available) ? available : len;
    if (read_len == 0) return 0;

    if (fifo->tail + read_len <= FIFO_BUFFER_SIZE) {
        memcpy(data, &fifo->buffer[fifo->tail], read_len);
        fifo->tail += read_len;
    } else {
        uint16_t first_part = FIFO_BUFFER_SIZE - fifo->tail;
        memcpy(data, &fifo->buffer[fifo->tail], first_part);
        uint16_t second_part = read_len - first_part;
        memcpy(&data[first_part], fifo->buffer, second_part);
        fifo->tail = second_part;
    }

    fifo->count -= read_len;
    return read_len;
}

void fifo_clear(FIFO_t *fifo)
{
    if (fifo == NULL) return;
    fifo->head  = 0;
    fifo->tail  = 0;
    fifo->count = 0;
}

/* ==================== DMA mode (ISR-safe, uses head/tail arithmetic) ==================== */

void fifo_dma_notify(FIFO_t *fifo, uint16_t new_bytes)
{
    if (fifo == NULL || new_bytes == 0) return;

    uint16_t old_head = fifo->head;
    uint16_t new_head = (old_head + new_bytes) % FIFO_BUFFER_SIZE;

    uint16_t free_space = (fifo->tail - old_head - 1 + FIFO_BUFFER_SIZE) % FIFO_BUFFER_SIZE;

    if (new_bytes > free_space) {
        fifo->tail = new_head;
    }

    fifo->head = new_head;
}

uint16_t fifo_dma_read(FIFO_t *fifo, uint8_t *buf, uint16_t max)
{
    if (fifo == NULL || buf == NULL || max == 0) return 0;

    uint16_t head = fifo->head;
    uint16_t tail = fifo->tail;

    uint16_t available = (head - tail + FIFO_BUFFER_SIZE) % FIFO_BUFFER_SIZE;
    if (available == 0) return 0;

    uint16_t to_read = (available > max) ? max : available;

    if (tail + to_read <= FIFO_BUFFER_SIZE) {
        memcpy(buf, &fifo->buffer[tail], to_read);
    } else {
        uint16_t first_part = FIFO_BUFFER_SIZE - tail;
        memcpy(buf, &fifo->buffer[tail], first_part);
        uint16_t second_part = to_read - first_part;
        memcpy(&buf[first_part], fifo->buffer, second_part);
    }

    fifo->tail = (tail + to_read) % FIFO_BUFFER_SIZE;
    return to_read;
}

uint16_t fifo_dma_available(FIFO_t *fifo)
{
    if (fifo == NULL) return 0;
    uint16_t head = fifo->head;
    uint16_t tail = fifo->tail;
    return (head - tail + FIFO_BUFFER_SIZE) % FIFO_BUFFER_SIZE;
}

void fifo_dma_flush(FIFO_t *fifo)
{
    if (fifo == NULL) return;
    fifo->tail = fifo->head;
}
