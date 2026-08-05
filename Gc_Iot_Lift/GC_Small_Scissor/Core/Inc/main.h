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

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

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
#define OUT_DOWN_VALVE_RELAY_GPIO_Port GPIOD
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
/* ===== DTU 任务日志开关：1=开启，0=关闭 ===== */
#define TAS_DTU_CONNECT_LOG     1
#define TAS_DTU_TRANSFER_LOG    1

/* ===== 举升任务日志开关：1=开启，0=关闭 ===== */
#define LIFT_CORE_DEBUG   1
#define LIFT_TASK_HEARTBEAT_LOG 1
#define IO_MAP_DEBUG      1
#define OP_LOG_DEBUG      1
#define SMALL_SCISSOR_DEBUG 1

#define LED_RUN_ON()      HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_RESET)
#define LED_RUN_OFF()     HAL_GPIO_WritePin(Led_Run_GPIO_Port, Led_Run_Pin, GPIO_PIN_SET)
#define LED_RUN_TOGGLE()  HAL_GPIO_TogglePin(Led_Run_GPIO_Port, Led_Run_Pin)

#define LED_COM_ON()      HAL_GPIO_WritePin(Led_Com_GPIO_Port, Led_Com_Pin, GPIO_PIN_RESET)
#define LED_COM_OFF()     HAL_GPIO_WritePin(Led_Com_GPIO_Port, Led_Com_Pin, GPIO_PIN_SET)
#define LED_COM_TOGGLE()  HAL_GPIO_TogglePin(Led_Com_GPIO_Port, Led_Com_Pin)

#define LED_POWER_ON()      HAL_GPIO_WritePin(Led_Power_GPIO_Port, Led_Power_Pin, GPIO_PIN_RESET)
#define LED_POWER_OFF()     HAL_GPIO_WritePin(Led_Power_GPIO_Port, Led_Power_Pin, GPIO_PIN_SET)
#define LED_POWER_TOGGLE()  HAL_GPIO_TogglePin(Led_Power_GPIO_Port, Led_Power_Pin)

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
