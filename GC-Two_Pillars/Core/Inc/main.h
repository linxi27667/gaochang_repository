/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* 包含c标准库 */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"

/* 通用框架 */
#include "FreeRTOS.h"
#include "task.h"

/* 项目组件 */
#include "elog.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* RS485 USART2 外部句柄 */
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EStop_Key_Pin GPIO_PIN_2
#define EStop_Key_GPIO_Port GPIOE
#define Upper_Limit_Pin GPIO_PIN_5
#define Upper_Limit_GPIO_Port GPIOE
#define MOTOR_Pin GPIO_PIN_8
#define MOTOR_GPIO_Port GPIOF
#define DROP_VALVE_Pin GPIO_PIN_8
#define DROP_VALVE_GPIO_Port GPIOD
#define Electromagnet_Pin GPIO_PIN_9
#define Electromagnet_GPIO_Port GPIOF
#define UNUSED_PD9_Pin GPIO_PIN_9
#define UNUSED_PD9_GPIO_Port GPIOD
#define UNUSED_PD10_Pin GPIO_PIN_10
#define UNUSED_PD10_GPIO_Port GPIOD
#define Led_Run_Pin GPIO_PIN_2
#define Led_Run_GPIO_Port GPIOG
#define Led_Com_Pin GPIO_PIN_3
#define Led_Com_GPIO_Port GPIOG
#define Led_Power_Pin GPIO_PIN_4
#define Led_Power_GPIO_Port GPIOG
#define RS485_Uart3_Dir_Pin GPIO_PIN_0
#define RS485_Uart3_Dir_GPIO_Port GPIOD
#define Up_Key_Pin GPIO_PIN_15
#define Up_Key_GPIO_Port GPIOG
#define W25Q_CS_Pin GPIO_PIN_6
#define W25Q_CS_GPIO_Port GPIOB
#define Down_Key_Pin GPIO_PIN_0
#define Down_Key_GPIO_Port GPIOE
#define Lock_Key_Pin GPIO_PIN_1
#define Lock_Key_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* PG2 运行指示灯：低电平点亮 */
#define LED_RUN_ON()          HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_RESET)
#define LED_RUN_OFF()         HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_SET)
#define LED_RUN_TOGGLE()      HAL_GPIO_TogglePin(Led_Run_GPIO_Port, Led_Run_Pin)

/* PG3 通信指示灯：低电平点亮 */
#define LED_COM_ON()          HAL_GPIO_WritePin(Led_Com_GPIO_Port, Led_Com_Pin, GPIO_PIN_RESET)
#define LED_COM_OFF()         HAL_GPIO_WritePin(Led_Com_GPIO_Port, Led_Com_Pin, GPIO_PIN_SET)
#define LED_COM_TOGGLE()      HAL_GPIO_TogglePin(Led_Com_GPIO_Port, Led_Com_Pin)

/* PG4 电源指示灯：低电平点亮，系统启动后常亮 */
#define LED_POWER_ON()        HAL_GPIO_WritePin(Led_Power_GPIO_Port, Led_Power_Pin, GPIO_PIN_RESET)
#define LED_POWER_OFF()       HAL_GPIO_WritePin(Led_Power_GPIO_Port, Led_Power_Pin, GPIO_PIN_SET)

/* ===== 各模块调试开关 ===== */
#define MOTOR_DEBUG          1
#define BALANCE_DEBUG        1
#define SAFETY_DEBUG         1
#define KEY_DEBUG            1
#define W25Q_DEBUG           1
#define CTRL_DEBUG           1
#define RS485_DEBUG          0

/* ===== 大剪举升机调试开关 ===== */
#define LIFT_CORE_DEBUG        1
#define LARGE_SCISSOR_DEBUG    1
#define IO_MAP_DEBUG           1
#define OP_LOG_DEBUG           1

/* ===== 两柱举升机调试开关 ===== */
#define DOUBLE_POST_DEBUG      1

/* ===== 硬件开关 ===== */
#define COLLISION_ENABLE             1
#define SECONDARY_DESCENT_ENABLE     0

/* 最大高度（mm），丝杆导程 8mm/脉冲 */
#define MAX_HEIGHT_MM      1000
#define MAX_PULSES         (MAX_HEIGHT_MM / 8)

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
