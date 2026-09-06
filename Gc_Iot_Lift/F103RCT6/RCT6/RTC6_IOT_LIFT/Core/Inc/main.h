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
#include "stm32f1xx_hal.h"

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
#define LED_RUN_Pin GPIO_PIN_13
#define LED_RUN_GPIO_Port GPIOC
#define OUT3_Pin GPIO_PIN_0
#define OUT3_GPIO_Port GPIOC
#define OUT2_Pin GPIO_PIN_1
#define OUT2_GPIO_Port GPIOC
#define OUT1_Pin GPIO_PIN_2
#define OUT1_GPIO_Port GPIOC
#define OUT0_Pin GPIO_PIN_3
#define OUT0_GPIO_Port GPIOC
#define ADC_12V_Pin GPIO_PIN_0
#define ADC_12V_GPIO_Port GPIOA
#define IN0_Pin GPIO_PIN_1
#define IN0_GPIO_Port GPIOA
#define IN1_Pin GPIO_PIN_2
#define IN1_GPIO_Port GPIOA
#define IN2_Pin GPIO_PIN_3
#define IN2_GPIO_Port GPIOA
#define IN3_Pin GPIO_PIN_4
#define IN3_GPIO_Port GPIOA
#define IN4_Pin GPIO_PIN_5
#define IN4_GPIO_Port GPIOA
#define IN5_Pin GPIO_PIN_6
#define IN5_GPIO_Port GPIOA
#define IN6_Pin GPIO_PIN_7
#define IN6_GPIO_Port GPIOA
#define IN7_Pin GPIO_PIN_4
#define IN7_GPIO_Port GPIOC
#define IN8_Pin GPIO_PIN_5
#define IN8_GPIO_Port GPIOC
#define IN9_Pin GPIO_PIN_0
#define IN9_GPIO_Port GPIOB
#define RELAY0_Pin GPIO_PIN_12
#define RELAY0_GPIO_Port GPIOB
#define RELAY1_Pin GPIO_PIN_13
#define RELAY1_GPIO_Port GPIOB
#define RELAY2_Pin GPIO_PIN_14
#define RELAY2_GPIO_Port GPIOB
#define RELAY3_Pin GPIO_PIN_15
#define RELAY3_GPIO_Port GPIOB
#define RELAY4_Pin GPIO_PIN_6
#define RELAY4_GPIO_Port GPIOC
#define RELAY5_Pin GPIO_PIN_7
#define RELAY5_GPIO_Port GPIOC
#define FRAM_SCL_Pin GPIO_PIN_6
#define FRAM_SCL_GPIO_Port GPIOB
#define FRAM_SDA_Pin GPIO_PIN_7
#define FRAM_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
