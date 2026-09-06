#ifndef __APP_RS485_H__
#define __APP_RS485_H__

#include "main.h"
#include "bsp_uart_dma.h"

/* ==================== RS485 协议层配置 ==================== */

#define RS485_TX_TIMEOUT_MS     100
#define RS485_MUTEX_TIMEOUT_MS  1000

/* ==================== 全局句柄 ==================== */

extern UartDMA_HandleTypeDef g_rs485_uart;

/* ==================== API ==================== */

uint8_t  RS485_Init(void);
uint8_t  RS485_Send(uint8_t *data, uint16_t len);
uint16_t RS485_Recv(uint8_t *buf, uint16_t max);
uint16_t RS485_Available(void);
void     RS485_Flush(void);
void     RS485_OnRxEvent(UART_HandleTypeDef *huart, uint16_t Size);

#endif
