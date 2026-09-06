/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file (STM32F103RCT6 GC-Screw_Lift)
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

/* 业务别名：光耦输入低有效 */
#define Up_Key_Pin                IN0_Pin
#define Up_Key_GPIO_Port          IN0_GPIO_Port
#define Down_Key_Pin              IN1_Pin
#define Down_Key_GPIO_Port        IN1_GPIO_Port
#define Stop_Key_Pin              IN2_Pin
#define Stop_Key_GPIO_Port        IN2_GPIO_Port
#define Left_Up_Safety_Pin        IN3_Pin
#define Left_Up_Safety_GPIO_Port  IN3_GPIO_Port
#define Right_Up_Safety_Pin       IN4_Pin
#define Right_Up_Safety_GPIO_Port IN4_GPIO_Port
#define Left_Down_Safety_Pin      IN5_Pin
#define Left_Down_Safety_GPIO_Port  IN5_GPIO_Port
#define Right_Down_Safety_Pin     IN6_Pin
#define Right_Down_Safety_GPIO_Port IN6_GPIO_Port

/* 兼容 F407 宏名：PC13 高电平点亮 */
#define Led_Run_Pin               LED_RUN_Pin
#define Led_Run_GPIO_Port         LED_RUN_GPIO_Port
#define LED_RUN_ON()              HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, GPIO_PIN_SET)
#define LED_RUN_OFF()             HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, GPIO_PIN_RESET)
#define LED_RUN_TOGGLE()          HAL_GPIO_TogglePin(LED_RUN_GPIO_Port, LED_RUN_Pin)

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

/* 最大高度（mm），丝杆导程 8mm/脉冲 */
#define MAX_HEIGHT_MM      1000
#define MAX_PULSES         (MAX_HEIGHT_MM / 8)

/* USART1 外部句柄（DTU） */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
