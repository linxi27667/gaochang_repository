/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c (STM32F103RCT6 GC_Thin_Scissor)
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

#include "elog.h"
/* USER CODE END Includes */

void Error_Handler(void);

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

#define BTN_UP_Pin                IN0_Pin
#define BTN_UP_GPIO_Port          IN0_GPIO_Port
#define BTN_DOWN_Pin              IN1_Pin
#define BTN_DOWN_GPIO_Port        IN1_GPIO_Port
#define BTN_LOCK_Pin              IN2_Pin
#define BTN_LOCK_GPIO_Port        IN2_GPIO_Port
#define ESTOP_NC_Pin              IN3_Pin
#define ESTOP_NC_GPIO_Port        IN3_GPIO_Port
#define BTN_REFILL_Pin            IN4_Pin
#define BTN_REFILL_GPIO_Port      IN4_GPIO_Port
#define LIMIT_UP_Pin              IN5_Pin
#define LIMIT_UP_GPIO_Port        IN5_GPIO_Port
#define LIMIT_DOWN_Pin            IN6_Pin
#define LIMIT_DOWN_GPIO_Port      IN6_GPIO_Port
#define PHOTO_EYE_Pin             IN7_Pin
#define PHOTO_EYE_GPIO_Port       IN7_GPIO_Port

#define OUT_MOTOR_RELAY_Pin       RELAY0_Pin
#define OUT_MOTOR_RELAY_GPIO_Port RELAY0_GPIO_Port
#define OUT_AIR_VALVE_Pin         RELAY1_Pin
#define OUT_AIR_VALVE_GPIO_Port   RELAY1_GPIO_Port
#define OUT_DOWN_VALVE_RELAY_Pin  RELAY2_Pin
#define OUT_DOWN_VALVE_RELAY_GPIO_Port RELAY2_GPIO_Port

#define Led_Run_Pin               LED_RUN_Pin
#define Led_Run_GPIO_Port         LED_RUN_GPIO_Port
#define Led_Com_Pin               LED_RUN_Pin
#define Led_Com_GPIO_Port         LED_RUN_GPIO_Port
#define Led_Power_Pin             LED_RUN_Pin
#define Led_Power_GPIO_Port       LED_RUN_GPIO_Port

#define LED_RUN_ON()              HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, GPIO_PIN_SET)
#define LED_RUN_OFF()             HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, GPIO_PIN_RESET)
#define LED_RUN_TOGGLE()          HAL_GPIO_TogglePin(LED_RUN_GPIO_Port, LED_RUN_Pin)
#define LED_COM_ON()              do { } while (0)
#define LED_COM_OFF()             do { } while (0)
#define LED_COM_TOGGLE()          do { } while (0)
#define LED_POWER_ON()            do { } while (0)
#define LED_POWER_OFF()           do { } while (0)
#define LED_POWER_TOGGLE()        do { } while (0)

#define TAS_DTU_CONNECT_LOG       1
#define TAS_DTU_TRANSFER_LOG      1
#define LIFT_CORE_DEBUG           1
#define LIFT_TASK_HEARTBEAT_LOG   1
#define THIN_SCISSOR_DEBUG        1
#define IO_MAP_DEBUG              1
#define OP_LOG_DEBUG              1
#define W25Q_DEBUG                1

#define MAX_PULSES                0U

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
