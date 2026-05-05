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

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define W25Q_CS_Pin GPIO_PIN_4
#define W25Q_CS_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* ===== 各模块独立调试开关 - 默认全关，需调试哪个就开哪个 ===== */
#define MOTOR_DEBUG          0   /* motor.c 启停日志（静默，由 dri_motor 统一输出） */
#define BALANCE_DEBUG        1   /* balance.c 平衡算法日志 */
#define SAFETY_DEBUG         1   /* safety.c 安全保护日志 */
#define KEY_DEBUG            1   /* key.c 按键+蜂鸣器日志 */
#define W25Q_DEBUG           1   /* app_w25qxx.c W25Q存取日志 */
#define CTRL_DEBUG           1   /* dri_motor.c 控制任务日志 */

/* ===== 硬件开关：真机接好传感器后改为 1 ===== */
#define COLLISION_ENABLE             0   /* 防碰杆实物（调试阶段关） */
#define SECONDARY_DESCENT_ENABLE     0   /* 二次下降保护（需确认键，调试关） */

/* SIM_COLLISION_HEIGHT_MM 见 Driver/Inc/dri_debug.h */   /* 上升到 500mm 触发防碰杆 */

/* 最大高度（mm），丝杆导程 6mm/脉冲 */
#define MAX_HEIGHT_MM      1000
#define MAX_PULSES         (MAX_HEIGHT_MM / 6)    /* 4000/6 ≈ 666 */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
