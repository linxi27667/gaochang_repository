/**
 * @file bsp_uart.c
 * @brief HAL UART ReceiveToIdle DMA wrapper with RX/TX FIFO buffering.
 */
#include "bsp_uart.h"
#include <string.h>

static bsp_uart_t *g_uart_list[BSP_UART_MAX_PORTS];

static uint32_t BSP_UART_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void BSP_UART_ExitCritical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static bsp_uart_t *BSP_UART_FindByHandle(UART_HandleTypeDef *huart)
{
    if (huart == NULL) return NULL;

    for (uint8_t i = 0; i < BSP_UART_MAX_PORTS; i++) {
        if ((g_uart_list[i] != NULL) && (g_uart_list[i]->huart == huart)) {
            return g_uart_list[i];
        }
    }

    return NULL;
}

static bsp_uart_status_t BSP_UART_Register(bsp_uart_t *dev)
{
    for (uint8_t i = 0; i < BSP_UART_MAX_PORTS; i++) {
        if (g_uart_list[i] == dev) {
            return BSP_UART_OK;
        }
    }

    for (uint8_t i = 0; i < BSP_UART_MAX_PORTS; i++) {
        if (g_uart_list[i] == NULL) {
            g_uart_list[i] = dev;
            return BSP_UART_OK;
        }
    }

    return BSP_UART_ERR_FULL;
}

static void BSP_UART_Unregister(bsp_uart_t *dev)
{
    for (uint8_t i = 0; i < BSP_UART_MAX_PORTS; i++) {
        if (g_uart_list[i] == dev) {
            g_uart_list[i] = NULL;
            return;
        }
    }
}

static uint16_t BSP_UART_WriteToRxFifo(bsp_uart_t *dev, const uint8_t *data, uint16_t len)
{
    uint16_t written = fifo_write_bytes(&dev->rx_fifo, data, len);
    dev->stats.rx_byte_count += written;

    if (written < len) {
        dev->stats.rx_drop_count += (uint32_t)(len - written);
    }

    return written;
}

static void BSP_UART_PushDmaRange(bsp_uart_t *dev, uint16_t start, uint16_t end)
{
    if (end <= start) return;
    (void)BSP_UART_WriteToRxFifo(dev, &dev->rx_dma_buf[start], (uint16_t)(end - start));
}

static void BSP_UART_UpdateRxFromDma(bsp_uart_t *dev, uint16_t pos)
{
    uint16_t last_pos;

    if ((dev == NULL) || (pos > dev->rx_dma_size)) return;

    last_pos = dev->rx_last_pos;
    if (pos == last_pos) return;

    if (pos > last_pos) {
        BSP_UART_PushDmaRange(dev, last_pos, pos);
    } else {
        BSP_UART_PushDmaRange(dev, last_pos, dev->rx_dma_size);
        BSP_UART_PushDmaRange(dev, 0U, pos);
    }

    dev->rx_last_pos = pos;
}

static inline void BSP_UART_DirTx(const bsp_uart_t *dev)
{
    if (dev->dir_port != NULL) {
        HAL_GPIO_WritePin(dev->dir_port, dev->dir_pin, GPIO_PIN_SET);
    }
}

static inline void BSP_UART_DirRx(const bsp_uart_t *dev)
{
    if (dev->dir_port != NULL) {
        HAL_GPIO_WritePin(dev->dir_port, dev->dir_pin, GPIO_PIN_RESET);
    }
}

static void BSP_UART_StartNextTx(bsp_uart_t *dev)
{
    uint16_t len;

    if ((dev == NULL) || (dev->huart == NULL) || dev->tx_busy) {
        return;
    }

    len = fifo_read_bytes(&dev->tx_fifo, dev->tx_dma_buf, BSP_UART_TX_DMA_CHUNK_SIZE);
    if (len == 0U) {
        return;
    }

    dev->tx_busy = true;
    dev->tx_dma_len = len;
    BSP_UART_DirTx(dev);

    if (HAL_UART_Transmit_DMA(dev->huart, dev->tx_dma_buf, len) != HAL_OK) {
        dev->tx_busy = false;
        dev->tx_dma_len = 0U;
        dev->stats.error_count++;
        dev->stats.tx_drop_count += len;
        BSP_UART_DirRx(dev);
    }
}

bsp_uart_status_t BSP_UART_Init(bsp_uart_t *dev,
                                UART_HandleTypeDef *huart,
                                uint8_t *rx_dma_buf,
                                uint16_t rx_dma_size)
{
    bsp_uart_status_t reg_status;

    if ((dev == NULL) || (huart == NULL) || (rx_dma_buf == NULL) || (rx_dma_size == 0U)) {
        return BSP_UART_ERR_PARAM;
    }

    memset(dev, 0, sizeof(*dev));

    dev->huart       = huart;
    dev->dir_port    = NULL;
    dev->dir_pin     = 0U;
    dev->rx_dma_buf  = rx_dma_buf;
    dev->rx_dma_size = rx_dma_size;
    dev->rx_last_pos = 0U;
    dev->rx_started  = false;
    dev->tx_busy     = false;
    dev->tx_dma_len  = 0U;
    fifo_init(&dev->rx_fifo);
    fifo_init(&dev->tx_fifo);

    reg_status = BSP_UART_Register(dev);
    if (reg_status != BSP_UART_OK) {
        return reg_status;
    }

    return BSP_UART_RestartRx(dev);
}

bsp_uart_status_t BSP_UART_DeInit(bsp_uart_t *dev)
{
    if ((dev == NULL) || (dev->huart == NULL)) return BSP_UART_ERR_PARAM;

    (void)HAL_UART_AbortReceive(dev->huart);
    (void)HAL_UART_AbortTransmit(dev->huart);
    dev->rx_started = false;
    dev->tx_busy = false;
    dev->tx_dma_len = 0U;
    BSP_UART_Unregister(dev);

    return BSP_UART_OK;
}

bsp_uart_status_t BSP_UART_RestartRx(bsp_uart_t *dev)
{
    HAL_StatusTypeDef status;

    if ((dev == NULL) || (dev->huart == NULL) ||
        (dev->rx_dma_buf == NULL) || (dev->rx_dma_size == 0U)) {
        return BSP_UART_ERR_PARAM;
    }

    dev->rx_last_pos = 0U;
    status = HAL_UARTEx_ReceiveToIdle_DMA(dev->huart, dev->rx_dma_buf, dev->rx_dma_size);
    if (status != HAL_OK) {
        dev->rx_started = false;
        return BSP_UART_ERR_HAL;
    }

#if BSP_UART_DISABLE_DMA_HT
    if (dev->huart->hdmarx != NULL) {
        __HAL_DMA_DISABLE_IT(dev->huart->hdmarx, DMA_IT_HT);
    }
#endif

    dev->rx_started = true;
    dev->stats.rx_restart_count++;
    return BSP_UART_OK;
}

void BSP_UART_SetDirection(bsp_uart_t *dev, GPIO_TypeDef *port, uint16_t pin)
{
    if (dev == NULL) return;

    dev->dir_port = port;
    dev->dir_pin  = pin;

    BSP_UART_DirRx(dev);
}

void BSP_UART_SetRxCallback(bsp_uart_t *dev, bsp_uart_rx_cb_t cb)
{
    if (dev == NULL) return;
    dev->rx_cb = cb;
}

void BSP_UART_SetTxDoneCallback(bsp_uart_t *dev, bsp_uart_tx_done_cb_t cb)
{
    if (dev == NULL) return;
    dev->tx_done_cb = cb;
}

void BSP_UART_SetErrorCallback(bsp_uart_t *dev, bsp_uart_error_cb_t cb)
{
    if (dev == NULL) return;
    dev->error_cb = cb;
}

uint16_t BSP_UART_Available(bsp_uart_t *dev)
{
    uint16_t available;
    uint32_t primask;

    if (dev == NULL) return 0U;

    primask = BSP_UART_EnterCritical();
    available = fifo_get_count(&dev->rx_fifo);
    BSP_UART_ExitCritical(primask);

    return available;
}

uint16_t BSP_UART_Read(bsp_uart_t *dev, uint8_t *buf, uint16_t max_len)
{
    uint16_t len;
    uint32_t primask;

    if ((dev == NULL) || (buf == NULL) || (max_len == 0U)) return 0U;

    primask = BSP_UART_EnterCritical();
    len = fifo_read_bytes(&dev->rx_fifo, buf, max_len);
    BSP_UART_ExitCritical(primask);

    return len;
}

bool BSP_UART_ReadByte(bsp_uart_t *dev, uint8_t *data)
{
    bool ret;
    uint32_t primask;

    if ((dev == NULL) || (data == NULL)) return false;

    primask = BSP_UART_EnterCritical();
    ret = fifo_read_byte(&dev->rx_fifo, data);
    BSP_UART_ExitCritical(primask);

    return ret;
}

void BSP_UART_FlushRx(bsp_uart_t *dev)
{
    uint32_t primask;

    if (dev == NULL) return;

    primask = BSP_UART_EnterCritical();
    fifo_clear(&dev->rx_fifo);
    BSP_UART_ExitCritical(primask);
}

void BSP_UART_FlushTx(bsp_uart_t *dev)
{
    uint32_t primask;

    if (dev == NULL) return;

    primask = BSP_UART_EnterCritical();
    fifo_clear(&dev->tx_fifo);
    BSP_UART_ExitCritical(primask);
}

bsp_uart_status_t BSP_UART_SendBlocking(bsp_uart_t *dev,
                                        const uint8_t *data,
                                        uint16_t len,
                                        uint32_t timeout_ms)
{
    HAL_StatusTypeDef hal_ret;

    if ((dev == NULL) || (dev->huart == NULL) || (data == NULL) || (len == 0U)) {
        return BSP_UART_ERR_PARAM;
    }

    BSP_UART_DirTx(dev);
    hal_ret = HAL_UART_Transmit(dev->huart, (uint8_t *)data, len, timeout_ms);
    BSP_UART_DirRx(dev);

    return (hal_ret == HAL_OK) ? BSP_UART_OK : BSP_UART_ERR_HAL;
}

bsp_uart_status_t BSP_UART_SendDMA(bsp_uart_t *dev, const uint8_t *data, uint16_t len)
{
    if ((dev == NULL) || (dev->huart == NULL) || (data == NULL) || (len == 0U)) {
        return BSP_UART_ERR_PARAM;
    }

    return (BSP_UART_Write(dev, data, len) == len) ? BSP_UART_OK : BSP_UART_ERR_FULL;
}

uint16_t BSP_UART_Write(bsp_uart_t *dev, const uint8_t *data, uint16_t len)
{
    uint16_t written;
    uint32_t primask;

    if ((dev == NULL) || (dev->huart == NULL) || (data == NULL) || (len == 0U)) {
        return 0U;
    }

    primask = BSP_UART_EnterCritical();
    written = fifo_write_bytes(&dev->tx_fifo, data, len);
    dev->stats.tx_byte_count += written;
    if (written < len) {
        dev->stats.tx_drop_count += (uint32_t)(len - written);
    }
    BSP_UART_StartNextTx(dev);
    BSP_UART_ExitCritical(primask);

    return written;
}

uint16_t BSP_UART_WriteString(bsp_uart_t *dev, const char *str)
{
    if (str == NULL) return 0U;
    return BSP_UART_Write(dev, (const uint8_t *)str, (uint16_t)strlen(str));
}

bool BSP_UART_IsTxBusy(const bsp_uart_t *dev)
{
    if (dev == NULL) return false;
    return dev->tx_busy;
}

uint16_t BSP_UART_TxPending(bsp_uart_t *dev)
{
    uint16_t pending;
    uint32_t primask;

    if (dev == NULL) return 0U;

    primask = BSP_UART_EnterCritical();
    pending = fifo_get_count(&dev->tx_fifo);
    if (dev->tx_busy) {
        pending = (uint16_t)(pending + dev->tx_dma_len);
    }
    BSP_UART_ExitCritical(primask);

    return pending;
}

void BSP_UART_ClearStats(bsp_uart_t *dev)
{
    if (dev == NULL) return;
    memset((void *)&dev->stats, 0, sizeof(dev->stats));
}

const bsp_uart_stats_t *BSP_UART_GetStats(const bsp_uart_t *dev)
{
    if (dev == NULL) return NULL;
    return &dev->stats;
}

void BSP_UART_OnHalRxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
    bsp_uart_t *dev = BSP_UART_FindByHandle(huart);
    uint16_t before;
    uint16_t after;
    uint16_t new_len;

    if (dev == NULL) return;

    before = fifo_get_count(&dev->rx_fifo);
    BSP_UART_UpdateRxFromDma(dev, size);
    after = fifo_get_count(&dev->rx_fifo);

    dev->stats.rx_event_count++;
    new_len = (after >= before) ? (uint16_t)(after - before) : 0U;

    if ((new_len > 0U) && (dev->rx_cb != NULL)) {
        dev->rx_cb(dev, new_len);
    }

    /* DMA 接收完成后必须重启，否则不再接收后续数据 */
    (void)BSP_UART_RestartRx(dev);
}

void BSP_UART_OnHalTxCplt(UART_HandleTypeDef *huart)
{
    bsp_uart_t *dev = BSP_UART_FindByHandle(huart);

    if (dev == NULL) return;

    dev->tx_busy = false;
    dev->tx_dma_len = 0U;
    dev->stats.tx_done_count++;

    BSP_UART_DirRx(dev);
    BSP_UART_StartNextTx(dev);

    if ((!dev->tx_busy) && (dev->tx_done_cb != NULL)) {
        dev->tx_done_cb(dev);
    }
}

void BSP_UART_OnHalError(UART_HandleTypeDef *huart)
{
    bsp_uart_t *dev = BSP_UART_FindByHandle(huart);
    uint32_t error_code;

    if (dev == NULL) return;

    error_code = huart->ErrorCode;
    dev->stats.error_count++;

    if ((error_code & HAL_UART_ERROR_ORE) != 0U) dev->stats.overrun_count++;
    if ((error_code & HAL_UART_ERROR_NE)  != 0U) dev->stats.noise_count++;
    if ((error_code & HAL_UART_ERROR_FE)  != 0U) dev->stats.frame_count++;
    if ((error_code & HAL_UART_ERROR_PE)  != 0U) dev->stats.parity_count++;

    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);

    if (dev->error_cb != NULL) {
        dev->error_cb(dev, error_code);
    }

    dev->tx_busy = false;
    dev->tx_dma_len = 0U;
    (void)HAL_UART_AbortReceive(huart);
    (void)HAL_UART_AbortTransmit(huart);
    BSP_UART_StartNextTx(dev);
    (void)BSP_UART_RestartRx(dev);
}
