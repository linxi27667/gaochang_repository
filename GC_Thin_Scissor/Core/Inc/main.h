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
#include "string.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
#define MAX_PULSES 30000U

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ESTOP_NC_Pin GPIO_PIN_2
#define ESTOP_NC_GPIO_Port GPIOE
#define BTN_REFILL_Pin GPIO_PIN_3
#define BTN_REFILL_GPIO_Port GPIOE
#define LIMIT_UP_Pin GPIO_PIN_5
#define LIMIT_UP_GPIO_Port GPIOE
#define LIMIT_DOWN_Pin GPIO_PIN_6
#define LIMIT_DOWN_GPIO_Port GPIOE
#define OUT_MOTOR_RELAY_Pin GPIO_PIN_8
#define OUT_MOTOR_RELAY_GPIO_Port GPIOF
#define OUT_DOWN_VALVE_RELAY_Pin GPIO_PIN_9
#define OUT_DOWN_VALVE_RELAY_GPIO_Port GPIOF
#define PHOTO_EYE_Pin GPIO_PIN_8
#define PHOTO_EYE_GPIO_Port GPIOE
#define OUT_AIR_VALVE_Pin GPIO_PIN_8
#define OUT_AIR_VALVE_GPIO_Port GPIOD
#define Led_Run_Pin GPIO_PIN_2
#define Led_Run_GPIO_Port GPIOG
#define Led_Com_Pin GPIO_PIN_3
#define Led_Com_GPIO_Port GPIOG
#define Led_Power_Pin GPIO_PIN_4
#define Led_Power_GPIO_Port GPIOG
#define RS485_Uart3_Dir_Pin GPIO_PIN_0
#define RS485_Uart3_Dir_GPIO_Port GPIOD
#define BTN_UP_Pin GPIO_PIN_15
#define BTN_UP_GPIO_Port GPIOG
#define W25Q_CS_Pin GPIO_PIN_6
#define W25Q_CS_GPIO_Port GPIOB
#define BTN_DOWN_Pin GPIO_PIN_0
#define BTN_DOWN_GPIO_Port GPIOE
#define BTN_LOCK_Pin GPIO_PIN_1
#define BTN_LOCK_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define Motor_Pin OUT_MOTOR_RELAY_Pin
#define Motor_GPIO_Port OUT_MOTOR_RELAY_GPIO_Port
#define Drop_Valve_Pin OUT_DOWN_VALVE_RELAY_Pin
#define Drop_Valve_GPIO_Port OUT_DOWN_VALVE_RELAY_GPIO_Port
#define Main_Air_Valve_Pin OUT_AIR_VALVE_Pin
#define Main_Air_Valve_GPIO_Port OUT_AIR_VALVE_GPIO_Port
#define Up_Key_Pin BTN_UP_Pin
#define Up_Key_GPIO_Port BTN_UP_GPIO_Port
#define Down_Key_Pin BTN_DOWN_Pin
#define Down_Key_GPIO_Port BTN_DOWN_GPIO_Port
#define Lock_Key_Pin BTN_LOCK_Pin
#define Lock_Key_GPIO_Port BTN_LOCK_GPIO_Port
#define EStop_Key_Pin ESTOP_NC_Pin
#define EStop_Key_GPIO_Port ESTOP_NC_GPIO_Port
#define Upper_Limit_Pin LIMIT_UP_Pin
#define Upper_Limit_GPIO_Port LIMIT_UP_GPIO_Port
#define Lower_Limit_Pin LIMIT_DOWN_Pin
#define Lower_Limit_GPIO_Port LIMIT_DOWN_GPIO_Port
#define Refill_Key_Pin BTN_REFILL_Pin
#define Refill_Key_GPIO_Port BTN_REFILL_GPIO_Port
#define Photoelectric_Pin PHOTO_EYE_Pin
#define Photoelectric_GPIO_Port PHOTO_EYE_GPIO_Port

#define LED_RUN_ON()          HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_RESET)
#define LED_RUN_OFF()         HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_SET)
#define LED_RUN_TOGGLE()      HAL_GPIO_TogglePin(Led_Run_GPIO_Port, Led_Run_Pin)
#define LED_COM_ON()          HAL_GPIO_WritePin(Led_Com_GPIO_Port, Led_Com_Pin, GPIO_PIN_RESET)
#define LED_COM_OFF()         HAL_GPIO_WritePin(Led_Com_GPIO_Port, Led_Com_Pin, GPIO_PIN_SET)
#define LED_COM_TOGGLE()      HAL_GPIO_TogglePin(Led_Com_GPIO_Port, Led_Com_Pin)
#define LED_POWER_ON()        HAL_GPIO_WritePin(Led_Power_GPIO_Port, Led_Power_Pin, GPIO_PIN_RESET)
#define LED_POWER_OFF()       HAL_GPIO_WritePin(Led_Power_GPIO_Port, Led_Power_Pin, GPIO_PIN_SET)

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
