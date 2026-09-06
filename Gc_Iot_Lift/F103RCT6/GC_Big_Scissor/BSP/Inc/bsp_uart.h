#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "fifo.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BSP_UART_MAX_PORTS
#define BSP_UART_MAX_PORTS 4U
#endif

#ifndef BSP_UART_DISABLE_DMA_HT
#define BSP_UART_DISABLE_DMA_HT 1U
#endif

#ifndef BSP_UART_TX_DMA_CHUNK_SIZE
#define BSP_UART_TX_DMA_CHUNK_SIZE 128U
#endif

typedef enum {
    BSP_UART_OK = 0,
    BSP_UART_ERR_PARAM,
    BSP_UART_ERR_FULL,
    BSP_UART_ERR_HAL,
    BSP_UART_ERR_NOT_FOUND
} bsp_uart_status_t;

typedef struct bsp_uart bsp_uart_t;

typedef void (*bsp_uart_rx_cb_t)(bsp_uart_t *dev, uint16_t new_len);
typedef void (*bsp_uart_tx_done_cb_t)(bsp_uart_t *dev);
typedef void (*bsp_uart_error_cb_t)(bsp_uart_t *dev, uint32_t error_code);

typedef struct {
    volatile uint32_t rx_event_count;
    volatile uint32_t rx_byte_count;
    volatile uint32_t rx_drop_count;
    volatile uint32_t tx_byte_count;
    volatile uint32_t tx_drop_count;
    volatile uint32_t tx_done_count;
    volatile uint32_t error_count;
    volatile uint32_t overrun_count;
    volatile uint32_t noise_count;
    volatile uint32_t frame_count;
    volatile uint32_t parity_count;
    volatile uint32_t rx_restart_count;
} bsp_uart_stats_t;

struct bsp_uart {
    UART_HandleTypeDef *huart;

    GPIO_TypeDef *dir_port;
    uint16_t      dir_pin;

    uint8_t  *rx_dma_buf;
    uint16_t  rx_dma_size;
    uint16_t  rx_last_pos;

    FIFO_t rx_fifo;
    FIFO_t tx_fifo;
    uint8_t tx_dma_buf[BSP_UART_TX_DMA_CHUNK_SIZE];
    uint16_t tx_dma_len;

    volatile bool rx_started;
    volatile bool tx_busy;

    bsp_uart_rx_cb_t      rx_cb;
    bsp_uart_tx_done_cb_t tx_done_cb;
    bsp_uart_error_cb_t   error_cb;

    bsp_uart_stats_t stats;
};

bsp_uart_status_t BSP_UART_Init(bsp_uart_t *dev,
                                UART_HandleTypeDef *huart,
                                uint8_t *rx_dma_buf,
                                uint16_t rx_dma_size);
bsp_uart_status_t BSP_UART_DeInit(bsp_uart_t *dev);
bsp_uart_status_t BSP_UART_RestartRx(bsp_uart_t *dev);

void BSP_UART_SetDirection(bsp_uart_t *dev, GPIO_TypeDef *port, uint16_t pin);
void BSP_UART_SetRxCallback(bsp_uart_t *dev, bsp_uart_rx_cb_t cb);
void BSP_UART_SetTxDoneCallback(bsp_uart_t *dev, bsp_uart_tx_done_cb_t cb);
void BSP_UART_SetErrorCallback(bsp_uart_t *dev, bsp_uart_error_cb_t cb);

uint16_t BSP_UART_Available(bsp_uart_t *dev);
uint16_t BSP_UART_Read(bsp_uart_t *dev, uint8_t *buf, uint16_t max_len);
bool BSP_UART_ReadByte(bsp_uart_t *dev, uint8_t *data);
void BSP_UART_FlushRx(bsp_uart_t *dev);
void BSP_UART_FlushTx(bsp_uart_t *dev);

bsp_uart_status_t BSP_UART_SendBlocking(bsp_uart_t *dev,
                                        const uint8_t *data,
                                        uint16_t len,
                                        uint32_t timeout_ms);
bsp_uart_status_t BSP_UART_SendDMA(bsp_uart_t *dev,
                                   const uint8_t *data,
                                   uint16_t len);
uint16_t BSP_UART_Write(bsp_uart_t *dev, const uint8_t *data, uint16_t len);
uint16_t BSP_UART_WriteString(bsp_uart_t *dev, const char *str);
bool BSP_UART_IsTxBusy(const bsp_uart_t *dev);
uint16_t BSP_UART_TxPending(bsp_uart_t *dev);

void BSP_UART_ClearStats(bsp_uart_t *dev);
const bsp_uart_stats_t *BSP_UART_GetStats(const bsp_uart_t *dev);

void BSP_UART_OnHalRxEvent(UART_HandleTypeDef *huart, uint16_t size);
void BSP_UART_OnHalTxCplt(UART_HandleTypeDef *huart);
void BSP_UART_OnHalError(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UART_H__ */
