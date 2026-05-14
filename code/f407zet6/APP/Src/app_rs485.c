#include "app_rs485.h"
#include "FreeRTOS.h"
#include "semphr.h"

#if RS485_DEBUG == 1
#include "elog.h"
#endif

/* ==================== HAL 层包装 ==================== */

static uint8_t hal_rx_dma_start(void *huart, void *hdma_rx, uint8_t *buf, uint16_t size)
{
    (void)hdma_rx;
    return HAL_UART_Receive_DMA((UART_HandleTypeDef *)huart, buf, size) == HAL_OK ? 1 : 0;
}

static uint8_t hal_tx_dma_start(void *huart, void *hdma_tx, const uint8_t *data, uint16_t size)
{
    (void)hdma_tx;
    return HAL_UART_Transmit_DMA((UART_HandleTypeDef *)huart, (uint8_t *)data, size) == HAL_OK ? 1 : 0;
}

static void hal_rx_abort(void *huart)
{
    HAL_UART_AbortReceive((UART_HandleTypeDef *)huart);
}

static void hal_tx_abort(void *huart)
{
    HAL_UART_AbortTransmit((UART_HandleTypeDef *)huart);
}

static void hal_idle_it_enable(void *huart)
{
    __HAL_UART_ENABLE_IT((UART_HandleTypeDef *)huart, UART_IT_IDLE);
}

static void hal_clear_errors(void *huart)
{
    UART_HandleTypeDef *h = (UART_HandleTypeDef *)huart;
    __HAL_UART_CLEAR_OREFLAG(h);
    __HAL_UART_CLEAR_FEFLAG(h);
    __HAL_UART_CLEAR_NEFLAG(h);
    __HAL_UART_CLEAR_PEFLAG(h);
}

/* ==================== RTOS 层包装 ==================== */

static void *rtos_mutex_create(void)
{
    return xSemaphoreCreateMutex();
}

static void *rtos_binary_sem_create(void)
{
    return xSemaphoreCreateBinary();
}

static uint8_t rtos_sem_take(void *sem, uint32_t timeout_ms)
{
    return xSemaphoreTake((SemaphoreHandle_t)sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ? 1 : 0;
}

static uint8_t rtos_sem_give(void *sem)
{
    return xSemaphoreGive((SemaphoreHandle_t)sem) == pdTRUE ? 1 : 0;
}

static uint8_t rtos_sem_give_from_isr(void *sem, void *px_woken)
{
    BaseType_t *px = (BaseType_t *)px_woken;
    *px = pdFALSE;
    return xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, px) == pdTRUE ? 1 : 0;
}

static void rtos_yield_from_isr(uint8_t yield)
{
    portYIELD_FROM_ISR((BaseType_t)yield);
}

/* ==================== 组装 ops 表 ==================== */

static const uart_dma_ops_t rs485_ops = {
    .rx_dma_start      = hal_rx_dma_start,
    .tx_dma_start      = hal_tx_dma_start,
    .rx_abort          = hal_rx_abort,
    .tx_abort          = hal_tx_abort,
    .idle_it_enable    = hal_idle_it_enable,
    .clear_errors      = hal_clear_errors,

    .mutex_create      = rtos_mutex_create,
    .binary_sem_create = rtos_binary_sem_create,
    .sem_take          = rtos_sem_take,
    .sem_give          = rtos_sem_give,
    .sem_give_from_isr = rtos_sem_give_from_isr,
    .yield_from_isr    = rtos_yield_from_isr,
};

/* ==================== 全局句柄 ==================== */

UartDMA_HandleTypeDef g_rs485_uart = {0};

/* ==================== RS485 协议层 API ==================== */

uint8_t RS485_Init(void)
{
    uint8_t ret = UartDMA_Init(&g_rs485_uart,
                               &huart2, &hdma_usart2_tx, &hdma_usart2_rx,
                               &rs485_ops);

#if RS485_DEBUG == 1
    if (ret)
        elog_i("RS485", "Init OK, BUF=%d", FIFO_BUFFER_SIZE);
    else
        elog_e("RS485", "Init FAILED");
#endif

    return ret;
}

uint8_t RS485_Send(uint8_t *data, uint16_t len)
{
    uint8_t ret = UartDMA_Send(&g_rs485_uart, data, len,
                               RS485_TX_TIMEOUT_MS, RS485_MUTEX_TIMEOUT_MS);

#if RS485_DEBUG == 1
    if (ret)
        elog_d("RS485", "TX %d bytes OK", len);
    else
        elog_e("RS485", "TX %d bytes FAILED", len);
#endif

    return ret;
}

uint16_t RS485_Recv(uint8_t *buf, uint16_t max)
{
    return UartDMA_Receive(&g_rs485_uart, buf, max);
}

uint16_t RS485_Available(void)
{
    return UartDMA_Available(&g_rs485_uart);
}

void RS485_Flush(void)
{
    UartDMA_FlushRx(&g_rs485_uart);
}

void RS485_OnRxEvent(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart != (UART_HandleTypeDef *)g_rs485_uart.huart) return;
    UartDMA_UpdateRxHead(&g_rs485_uart, Size);
}
