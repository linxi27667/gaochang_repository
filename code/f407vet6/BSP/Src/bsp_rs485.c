#include "bsp_rs485.h"
#include <string.h>

#if RS485_DEBUG == 1
#include "elog.h"
#endif

/* ==================== 全局句柄 ==================== */
RS485_HandleTypeDef rs485_handle = {0};

/* 接收缓冲区 */
static uint8_t rs485_rx_buf[RS485_RX_BUF_SIZE];

/* ==================== 应用层初始化 ==================== */

HAL_StatusTypeDef App_RS485_Init(void)
{
    rs485_handle.huart       = &huart2;
    rs485_handle.hdma_tx     = &hdma_usart2_tx;
    rs485_handle.hdma_rx     = &hdma_usart2_rx;
    rs485_handle.rx_buf      = rs485_rx_buf;
    rs485_handle.rx_buf_size = RS485_RX_BUF_SIZE;
    rs485_handle.rx_idle_callback = NULL;

    return RS485_Init(&rs485_handle);
}

/* ==================== 初始化 ==================== */

HAL_StatusTypeDef RS485_Init(RS485_HandleTypeDef *hrs485)
{
    /* 1. 创建FreeRTOS同步原语 */
    hrs485->tx_mutex    = xSemaphoreCreateMutex();
    hrs485->tx_done_sem = xSemaphoreCreateBinary();
    if (hrs485->tx_mutex == NULL || hrs485->tx_done_sem == NULL)
        return HAL_ERROR;

    /* 2. 启动DMA循环接收 */
    HAL_StatusTypeDef ret = HAL_UART_Receive_DMA(hrs485->huart,
                                                  hrs485->rx_buf,
                                                  hrs485->rx_buf_size);
    if (ret != HAL_OK) return ret;

    /* 3. 使能IDLE中断 */
    __HAL_UART_ENABLE_IT(hrs485->huart, UART_IT_IDLE);

    /* 4. 初始化状态 */
    hrs485->rx_head = 0;
    hrs485->rx_tail = 0;
    hrs485->err_overrun  = 0;
    hrs485->err_framing  = 0;
    hrs485->err_noise    = 0;

    #if RS485_DEBUG == 1
    elog_i("RS485", "Init OK, RX_BUF=%d", RS485_RX_BUF_SIZE);
    #endif

    return HAL_OK;
}

/* ==================== 反初始化 ==================== */

HAL_StatusTypeDef RS485_DeInit(RS485_HandleTypeDef *hrs485)
{
    HAL_UART_AbortReceive(hrs485->huart);
    HAL_UART_AbortTransmit(hrs485->huart);

    if (hrs485->tx_mutex != NULL) {
        vSemaphoreDelete(hrs485->tx_mutex);
        hrs485->tx_mutex = NULL;
    }
    if (hrs485->tx_done_sem != NULL) {
        vSemaphoreDelete(hrs485->tx_done_sem);
        hrs485->tx_done_sem = NULL;
    }

    return HAL_OK;
}

/* ==================== 发送函数（RTOS安全 + 超时自愈） ==================== */

HAL_StatusTypeDef RS485_Transmit(RS485_HandleTypeDef *hrs485,
                                  uint8_t *data, uint16_t len,
                                  uint32_t timeout_ms)
{
    /* 1. 获取互斥量（总线独占） */
    if (xSemaphoreTake(hrs485->tx_mutex,
                        pdMS_TO_TICKS(RS485_MUTEX_TIMEOUT_MS)) != pdTRUE)
        return HAL_TIMEOUT;

    /* 2. 清空信号量残留 */
    xSemaphoreTake(hrs485->tx_done_sem, 0);

    /* 3. 启动DMA发送（自动方向模块会自动切换到发送模式） */
    HAL_StatusTypeDef ret = HAL_UART_Transmit_DMA(hrs485->huart, data, len);
    if (ret != HAL_OK) {
        HAL_UART_AbortTransmit(hrs485->huart);  // 重置硬件状态
        xSemaphoreTake(hrs485->tx_done_sem, 0);
        xSemaphoreGive(hrs485->tx_mutex);

        #if RS485_DEBUG == 1
        elog_e("RS485", "Transmit DMA failed: %d", ret);
        #endif
        return ret;
    }

    /* 4. 等待TxCpltCallback释放信号量 */
    if (xSemaphoreTake(hrs485->tx_done_sem,
                        pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        HAL_UART_AbortTransmit(hrs485->huart);  // 终止DMA + 重置状态
        xSemaphoreTake(hrs485->tx_done_sem, 0);  // 清空幽灵信号
        xSemaphoreGive(hrs485->tx_mutex);

        #if RS485_DEBUG == 1
        elog_e("RS485", "Transmit timeout");
        #endif
        return HAL_TIMEOUT;
    }

    /* 5. 释放互斥量 */
    xSemaphoreGive(hrs485->tx_mutex);

    #if RS485_DEBUG == 1
    elog_d("RS485", "TX %d bytes OK", len);
    #endif

    return HAL_OK;
}

/* ==================== 接收函数（处理绕回） ==================== */

uint16_t RS485_Receive(RS485_HandleTypeDef *hrs485,
                       uint8_t *buf, uint16_t max_len)
{
    uint16_t head = hrs485->rx_head;
    uint16_t tail = hrs485->rx_tail;
    uint16_t size = hrs485->rx_buf_size;

    /* 1. 计算可读数据长度 */
    uint16_t available = (head - tail + size) % size;
    if (available == 0) return 0;

    /* 2. 限制读取长度 */
    uint16_t to_read = (available > max_len) ? max_len : available;

    /* 3. 处理绕回：分两段拷贝 */
    if (tail + to_read > size) {
        /* 第一段：tail → 数组末尾 */
        uint16_t first_part = size - tail;
        memcpy(buf, &hrs485->rx_buf[tail], first_part);

        /* 第二段：数组开头 → head */
        uint16_t second_part = to_read - first_part;
        memcpy(&buf[first_part], &hrs485->rx_buf[0], second_part);
    } else {
        /* 无绕回，直接拷贝 */
        memcpy(buf, &hrs485->rx_buf[tail], to_read);
    }

    /* 4. 更新tail（原子操作：关中断保护） */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    hrs485->rx_tail = (tail + to_read) % size;
    __set_PRIMASK(primask);

    return to_read;
}

/* ==================== 获取接收数据长度 ==================== */

uint16_t RS485_GetRxCount(RS485_HandleTypeDef *hrs485)
{
    uint16_t head = hrs485->rx_head;
    uint16_t tail = hrs485->rx_tail;
    uint16_t size = hrs485->rx_buf_size;
    return (head - tail + size) % size;
}

/* ==================== 清空接收缓冲区 ==================== */

void RS485_FlushRx(RS485_HandleTypeDef *hrs485)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    hrs485->rx_tail = hrs485->rx_head;
    __set_PRIMASK(primask);
}

/* ==================== IDLE中断处理 ==================== */

void RS485_IRQHandler(RS485_HandleTypeDef *hrs485)
{
    if (__HAL_UART_GET_FLAG(hrs485->huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(hrs485->huart);

        uint16_t dma_counter = __HAL_DMA_GET_COUNTER(hrs485->hdma_rx);
        uint16_t new_head = hrs485->rx_buf_size - dma_counter;
        uint16_t old_head = hrs485->rx_head;

        /* 帧长度 */
        uint16_t frame_len = (new_head - old_head + hrs485->rx_buf_size) % hrs485->rx_buf_size;

        /* 收到本帧前还有多少空间（预留 1 字节分隔空/满） */
        uint16_t free_space = (hrs485->rx_tail - old_head - 1 + hrs485->rx_buf_size) % hrs485->rx_buf_size;

        if (frame_len > free_space) {
            hrs485->rx_tail = new_head;  // 溢出，丢弃全部旧数据，重新开始
        }

        hrs485->rx_head = new_head;

        if (hrs485->rx_idle_callback)
            hrs485->rx_idle_callback(hrs485, hrs485->rx_head);
    }
}

/* ==================== HAL回调：发送完成 ==================== */

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == RS485_UART) {
        /* HAL已保证TC标志置位后才调用此回调 */
        /* 自动方向模块在TX空闲后硬件自动切回接收模式，无需软件操作 */

        /* 释放二值信号量，唤醒等待的任务 */
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(rs485_handle.tx_done_sem,
                              &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ==================== HAL回调：错误自愈 ==================== */

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == RS485_UART) {
        uint32_t error = HAL_UART_GetError(huart);

        /* 统计错误类型 */
        if (error & HAL_UART_ERROR_ORE)
            rs485_handle.err_overrun++;
        if (error & HAL_UART_ERROR_FE)
            rs485_handle.err_framing++;
        if (error & HAL_UART_ERROR_NE)
            rs485_handle.err_noise++;

        #if RS485_DEBUG == 1
        elog_e("RS485", "UART error: 0x%08lX (ORE=%lu FE=%lu)",
               error, rs485_handle.err_overrun, rs485_handle.err_framing);
        #endif

        /* 清除所有错误标志 */
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_PEFLAG(huart);

        /* 重启DMA接收（自愈） */
        HAL_UART_AbortReceive(huart);
        HAL_UART_Receive_DMA(huart,
                             rs485_handle.rx_buf,
                             rs485_handle.rx_buf_size);

        /* 重新使能IDLE中断 */
        __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
    }
}
