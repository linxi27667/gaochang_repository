#ifndef __BSP_RS485_H__
#define __BSP_RS485_H__

#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* ==================== 用户配置区 ==================== */
#define RS485_UART              USART2
#define RS485_BAUDRATE          115200

#define RS485_RX_BUF_SIZE       256     // 接收环形缓冲区大小(建议2的幂次)
#define RS485_TX_TIMEOUT_MS     100     // 发送超时(ms)
#define RS485_MUTEX_TIMEOUT_MS  1000    // 互斥量获取超时(ms)

/* ==================== 句柄结构体 ==================== */
typedef struct {
    UART_HandleTypeDef *huart;
    DMA_HandleTypeDef  *hdma_tx;
    DMA_HandleTypeDef  *hdma_rx;

    /* 接收环形缓冲区 (DMA循环模式) */
    uint8_t            *rx_buf;
    uint16_t            rx_buf_size;
    volatile uint16_t   rx_head;    // DMA写位置 (IDLE中断更新)
    volatile uint16_t   rx_tail;    // 软件读位置 (任务更新)

    /* FreeRTOS同步原语 */
    SemaphoreHandle_t   tx_mutex;       // 互斥量：总线独占
    SemaphoreHandle_t   tx_done_sem;    // 二值信号量：发送完成通知

    /* 错误计数（调试用） */
    volatile uint32_t   err_overrun;
    volatile uint32_t   err_framing;
    volatile uint32_t   err_noise;

    /* 回调 */
    void (*rx_idle_callback)(void *arg, uint16_t size);
} RS485_HandleTypeDef;

/* ==================== 全局句柄 ==================== */
extern RS485_HandleTypeDef rs485_handle;

/* ==================== API声明 ==================== */
HAL_StatusTypeDef App_RS485_Init(void);
HAL_StatusTypeDef RS485_Init(RS485_HandleTypeDef *hrs485);
HAL_StatusTypeDef RS485_DeInit(RS485_HandleTypeDef *hrs485);
HAL_StatusTypeDef RS485_Transmit(RS485_HandleTypeDef *hrs485,
                                  uint8_t *data, uint16_t len,
                                  uint32_t timeout_ms);
uint16_t RS485_Receive(RS485_HandleTypeDef *hrs485,
                       uint8_t *buf, uint16_t max_len);
uint16_t RS485_GetRxCount(RS485_HandleTypeDef *hrs485);
void RS485_FlushRx(RS485_HandleTypeDef *hrs485);

/* 中断相关（在stm32f4xx_it.c中调用） */
void RS485_IRQHandler(RS485_HandleTypeDef *hrs485);

#endif /* __BSP_RS485_H__ */
