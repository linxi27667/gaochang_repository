#ifndef KEY_H
#define KEY_H

#include <stdint.h>
#include "main.h"
#include "motor.h"   /* direction_t */

/* ===== 引脚 ===== */
#define KEY_UP_PORT         GPIOB
#define KEY_UP_PIN          GPIO_PIN_12
#define KEY_DOWN_PORT       GPIOB
#define KEY_DOWN_PIN        GPIO_PIN_13
#define KEY_STOP_PORT       GPIOB
#define KEY_STOP_PIN        GPIO_PIN_14
#define KEY_CONFIRM_PORT    GPIOB
#define KEY_CONFIRM_PIN     GPIO_PIN_15

/* ===== 电平 ===== */
#define KEY_PRESS   0
#define KEY_RELEASE 1

#define MAX_KEY_NUM 2   /* 调试只接 UP/DOWN，真机改 4 */
#define KEY_DEBOUNCE_TICKS  2   /* 2×10ms = 20ms */

#define BUZZER_PORT         GPIOB
#define BUZZER_PIN          GPIO_PIN_8

/* ===== 类型 ===== */
typedef enum {
    KEY_STATE_IDLE      = 0,   /* 空闲，等待按下 */
    KEY_STATE_DEBOUNCE  = 1,   /* 消抖中，TIM7 计时 20ms */
    KEY_STATE_CONFIRMED = 2,   /* 已确认按下，等待松开 */
} key_state_t;

typedef struct {
    key_state_t     state;          /* 当前状态 */
    GPIO_TypeDef   *port;
    uint16_t        pin;
    volatile uint8_t f_push;        /* 按下事件标志（上层消费后清零） */
    volatile uint8_t f_hold;        /* 保持标志（1=正在按住） */
    uint8_t         debounce_tick;  /* TIM7 消抖计数 */
} key_t;

/* ===== 全局 ===== */
extern key_t g_key[MAX_KEY_NUM];

/* ===== 命令（兼容旧接口）===== */
typedef struct {
    uint8_t      button_up;
    uint8_t      button_down;
    uint8_t      button_stop;
    uint8_t      button_confirm;
    direction_t  direction;
} key_command_t;
extern key_command_t g_command;

/* ===== API ===== */
void Key_Init(void);
void Key_Scan(void);
void Buzzer_On(void);
void Buzzer_Off(void);
void Buzzer_Beep(uint32_t duration_ms);
void Buzzer_Poll(void);

#endif
