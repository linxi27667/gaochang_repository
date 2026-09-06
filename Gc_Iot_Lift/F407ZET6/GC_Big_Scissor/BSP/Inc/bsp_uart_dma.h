#ifndef __BSP_UART_DMA_H__
#define __BSP_UART_DMA_H__

#include <stdint.h>
#include "fifo.h"
#include "main.h"

typedef struct {
    uint8_t (*rx_dma_start)     (void *huart, void *hdma_rx, uint8_t *buf, uint16_t size);
    uint8_t (*tx_dma_start)     (void *huart, void *hdma_tx, const uint8_t *data, uint16_t size);
    void    (*rx_abort)         (void *huart);
    void    (*tx_abort)         (void *huart);
    void    (*idle_it_enable)   (void *huart);
    void    (*clear_errors)     (void *huart);

    void*   (*mutex_create)     (void);
    void*   (*binary_sem_create)(void);
    uint8_t (*sem_take)         (void *sem, uint32_t timeout_ms);
    uint8_t (*sem_give)         (void *sem);
    uint8_t (*sem_give_from_isr)(void *sem, void *px_woken);
    void    (*yield_from_isr)   (uint8_t yield);
} uart_dma_ops_t;

typedef struct {
    void   *huart;
    void   *hdma_tx;
    void   *hdma_rx;
    FIFO_t  fifo;
    void   *tx_mutex;
    void   *tx_done_sem;
    const uart_dma_ops_t *ops;

    volatile uint32_t err_overrun;
    volatile uint32_t err_framing;
    volatile uint32_t err_noise;
} UartDMA_HandleTypeDef;

uint8_t  UartDMA_Init(UartDMA_HandleTypeDef *h, void *huart, void *hdma_tx, void *hdma_rx, const uart_dma_ops_t *ops);
uint8_t  UartDMA_DeInit(UartDMA_HandleTypeDef *h);

uint8_t  UartDMA_Send(UartDMA_HandleTypeDef *h, uint8_t *data, uint16_t len,
                      uint32_t tx_timeout_ms, uint32_t mutex_timeout_ms);

uint16_t UartDMA_Receive(UartDMA_HandleTypeDef *h, uint8_t *buf, uint16_t max);
uint16_t UartDMA_Available(UartDMA_HandleTypeDef *h);
void     UartDMA_FlushRx(UartDMA_HandleTypeDef *h);
void     UartDMA_UpdateRxHead(UartDMA_HandleTypeDef *h, uint16_t new_data_len);

void     BSP_UART_TxCpltCallback(struct __UART_HandleTypeDef *huart);
void     BSP_UART_ErrorCallback(struct __UART_HandleTypeDef *huart);

#endif
