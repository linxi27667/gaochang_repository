#include "bsp_uart_dma.h"

static UartDMA_HandleTypeDef *g_instance = NULL;

uint8_t UartDMA_Init(UartDMA_HandleTypeDef *h, void *huart, void *hdma_tx, void *hdma_rx, const uart_dma_ops_t *ops)
{
    if (h == NULL || huart == NULL || hdma_rx == NULL || ops == NULL) return 0;

    h->huart       = huart;
    h->hdma_tx     = hdma_tx;
    h->hdma_rx     = hdma_rx;
    h->ops         = ops;
    h->err_overrun = 0;
    h->err_framing = 0;
    h->err_noise   = 0;

    fifo_init(&h->fifo);

    h->tx_mutex    = ops->mutex_create();
    h->tx_done_sem = ops->binary_sem_create();
    if (h->tx_mutex == NULL || h->tx_done_sem == NULL) return 0;

    if (!ops->rx_dma_start(huart, hdma_rx, h->fifo.buffer, FIFO_BUFFER_SIZE)) return 0;

    ops->idle_it_enable(huart);

    g_instance = h;
    return 1;
}

uint8_t UartDMA_DeInit(UartDMA_HandleTypeDef *h)
{
    if (h == NULL || h->ops == NULL) return 0;

    h->ops->rx_abort(h->huart);
    h->ops->tx_abort(h->huart);

    if (g_instance == h) g_instance = NULL;
    return 1;
}

uint8_t UartDMA_Send(UartDMA_HandleTypeDef *h, uint8_t *data, uint16_t len,
                     uint32_t tx_timeout_ms, uint32_t mutex_timeout_ms)
{
    if (h == NULL || data == NULL || len == 0 || h->ops == NULL) return 0;

    const uart_dma_ops_t *o = h->ops;

    if (!o->sem_take(h->tx_mutex, mutex_timeout_ms)) return 0;

    o->sem_take(h->tx_done_sem, 0);

    if (!o->tx_dma_start(h->huart, h->hdma_tx, data, len)) {
        o->tx_abort(h->huart);
        o->sem_take(h->tx_done_sem, 0);
        o->sem_give(h->tx_mutex);
        return 0;
    }

    if (!o->sem_take(h->tx_done_sem, tx_timeout_ms)) {
        o->tx_abort(h->huart);
        o->sem_take(h->tx_done_sem, 0);
        o->sem_give(h->tx_mutex);
        return 0;
    }

    o->sem_give(h->tx_mutex);
    return 1;
}

uint16_t UartDMA_Receive(UartDMA_HandleTypeDef *h, uint8_t *buf, uint16_t max)
{
    if (h == NULL || buf == NULL || max == 0) return 0;
    return fifo_dma_read(&h->fifo, buf, max);
}

uint16_t UartDMA_Available(UartDMA_HandleTypeDef *h)
{
    if (h == NULL) return 0;
    return fifo_dma_available(&h->fifo);
}

void UartDMA_FlushRx(UartDMA_HandleTypeDef *h)
{
    if (h == NULL) return;
    fifo_dma_flush(&h->fifo);
}

void UartDMA_UpdateRxHead(UartDMA_HandleTypeDef *h, uint16_t new_data_len)
{
    if (h == NULL) return;
    fifo_dma_notify(&h->fifo, new_data_len);
}

static void onTxComplete(UartDMA_HandleTypeDef *h)
{
    if (h == NULL || h->ops == NULL) return;
    uint8_t woken = 0;
    h->ops->sem_give_from_isr(h->tx_done_sem, &woken);
    h->ops->yield_from_isr(woken);
}

static void onError(UartDMA_HandleTypeDef *h)
{
    if (h == NULL || h->ops == NULL) return;
    h->ops->clear_errors(h->huart);
    h->ops->rx_abort(h->huart);
    h->ops->rx_dma_start(h->huart, h->hdma_rx, h->fifo.buffer, FIFO_BUFFER_SIZE);
    h->ops->idle_it_enable(h->huart);
}

void BSP_UART_TxCpltCallback(struct __UART_HandleTypeDef *huart)
{
    if (g_instance == NULL || g_instance->huart != (void *)huart) return;
    onTxComplete(g_instance);
}

void BSP_UART_ErrorCallback(struct __UART_HandleTypeDef *huart)
{
    if (g_instance == NULL || g_instance->huart != (void *)huart) return;
    onError(g_instance);
}
