/*
 * drv_gpio.h - GPIO 引脚宏定义
 *
 * 所有引脚集中定义，业务层通过宏调用，不直接写 HAL_GPIO_xxxPin
 */
#ifndef DRV_GPIO_H
#define DRV_GPIO_H

#include "stm32f4xx_hal.h"

/* ================= 输入引脚 ================= */
/* 业务层调用：if (E_STOP_Read() == GPIO_PIN_RESET) { ... } */

#define E_STOP_Read()           HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0)
#define UPPER_LIM_1_Read()      HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1)
#define UPPER_LIM_2_Read()      HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2)
#define LOWER_LIM_1_Read()      HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3)
#define LOWER_LIM_2_Read()      HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4)
#define NUT_WEAR_Read()         HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_5)
#define KEY_RISE_Read()         HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7)
#define KEY_FALL_Read()         HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_8)
#define ANTI_COLLISION_Read()   HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6)
#define QS1_MODE_Read()         HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15)
#define PB2_CONFIRM_Read()      HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2)

/* ================= 输出引脚 ================= */

#define KM1_RISE_ON()           HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET)
#define KM1_RISE_OFF()          HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET)
#define KM1_FALL_ON()           HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET)
#define KM1_FALL_OFF()          HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_RESET)
#define KM2_RISE_ON()           HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET)
#define KM2_RISE_OFF()          HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET)
#define KM2_FALL_ON()           HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET)
#define KM2_FALL_OFF()          HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_RESET)
#define LED_ON()                HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_SET)
#define LED_OFF()               HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET)
#define BUZZER_ON()             HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, GPIO_PIN_SET)
#define BUZZER_OFF()            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, GPIO_PIN_RESET)

#define MOTOR_ALL_OFF()         do { \
                                    KM1_RISE_OFF(); KM1_FALL_OFF(); \
                                    KM2_RISE_OFF(); KM2_FALL_OFF(); \
                                } while(0)

/* ================= GPIO 初始化 ================= */
void Drv_GPIO_Init(void);

#endif /* DRV_GPIO_H */
