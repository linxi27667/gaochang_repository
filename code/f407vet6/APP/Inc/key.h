#ifndef KEY_H
#define KEY_H

#include <stdint.h>
#include "main.h"

/* ==================== 按键指令 ==================== */
typedef struct {
    uint8_t button_up;       /* 上升键 */
    uint8_t button_down;     /* 下降键 */
    uint8_t button_stop;     /* 停止键 */
    uint8_t button_confirm;  /* 二次下降确认键 */
    uint8_t direction;       /* 当前方向 0=停 1=升 2=降 */
} key_command_t;

/* ==================== 全局变量 ==================== */
extern key_command_t g_command;

/* ==================== 按键引脚宏（用户可按实际接线修改）==================== */

#define BUTTON_UP_PORT      GPIOB
#define BUTTON_UP_PIN       GPIO_PIN_12

#define BUTTON_DOWN_PORT    GPIOB
#define BUTTON_DOWN_PIN     GPIO_PIN_13

#define BUTTON_STOP_PORT    GPIOB
#define BUTTON_STOP_PIN     GPIO_PIN_14

#define BUTTON_CONFIRM_PORT GPIOB
#define BUTTON_CONFIRM_PIN  GPIO_PIN_15

/* ==================== 蜂鸣器引脚宏 ==================== */

#define BUZZER_PORT         GPIOB
#define BUZZER_PIN          GPIO_PIN_8

/* ==================== API ==================== */

void Key_Init(void);
void Key_Scan(void);

void Buzzer_On(void);
void Buzzer_Off(void);
void Buzzer_Beep(uint32_t duration_ms);
void Buzzer_Poll(void);

#endif
