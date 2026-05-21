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
#include "cmsis_os.h"

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
#define Left_Up_Safety_Pin GPIO_PIN_5
#define Left_Up_Safety_GPIO_Port GPIOE
#define Right_Up_Safety_Pin GPIO_PIN_6
#define Right_Up_Safety_GPIO_Port GPIOE
#define Left_Down_Safety_Pin GPIO_PIN_7
#define Left_Down_Safety_GPIO_Port GPIOE
#define Right_Down_Safety_Pin GPIO_PIN_8
#define Right_Down_Safety_GPIO_Port GPIOE
#define Left_Moter_Realy_Pin GPIO_PIN_8
#define Left_Moter_Realy_GPIO_Port GPIOD
#define Right_Moter_Realy_Pin GPIO_PIN_9
#define Right_Moter_Realy_GPIO_Port GPIOD
#define Up_Realy_Pin GPIO_PIN_10
#define Up_Realy_GPIO_Port GPIOD
#define Down_Realy_Pin GPIO_PIN_11
#define Down_Realy_GPIO_Port GPIOD
#define Led_Run_Pin GPIO_PIN_2
#define Led_Run_GPIO_Port GPIOG
#define Led_Com_Pin GPIO_PIN_3
#define Led_Com_GPIO_Port GPIOG
#define Led_Power_Pin GPIO_PIN_4
#define Led_Power_GPIO_Port GPIOG
#define W25Q_CS_Pin GPIO_PIN_6
#define W25Q_CS_GPIO_Port GPIOB
#define Up_Key_Pin GPIO_PIN_0
#define Up_Key_GPIO_Port GPIOE
#define Down_Key_Pin GPIO_PIN_1
#define Down_Key_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* PG2 运行指示灯：低电平点亮 */
#define LED_RUN_ON()          HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_RESET)
#define LED_RUN_OFF()         HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_SET)
#define LED_RUN_TOGGLE()      HAL_GPIO_TogglePin(Led_Run_GPIO_Port, Led_Run_Pin)

/* ===== 各模块调试开关 ===== */
#define MOTOR_DEBUG          1
#define BALANCE_DEBUG        1
#define SAFETY_DEBUG         1
#define KEY_DEBUG            1
#define W25Q_DEBUG           1
#define CTRL_DEBUG           1
#define RS485_DEBUG          0

/* ===== 硬件开关 ===== */
#define COLLISION_ENABLE             1
#define SECONDARY_DESCENT_ENABLE     0

/* 最大高度（mm），丝杆导程 6mm/脉冲 */
#define MAX_HEIGHT_MM      1000
#define MAX_PULSES         (MAX_HEIGHT_MM / 6)

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
