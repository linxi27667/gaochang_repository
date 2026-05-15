#ifndef KEY_H
#define KEY_H

#include <stdint.h>
#include "main.h"
#include "motor.h"

/* ===== 按键引脚：PE0=上升 PE1=下降 PE2=停止 ===== */
#define KEY_UP_PORT         GPIOE
#define KEY_UP_PIN          GPIO_PIN_0
#define KEY_DOWN_PORT       GPIOE
#define KEY_DOWN_PIN        GPIO_PIN_1
#define KEY_STOP_PORT       GPIOE
#define KEY_STOP_PIN        GPIO_PIN_2

#define KEY_PRESS   0
#define KEY_RELEASE 1

#define MAX_KEY_NUM 3

typedef enum {
    KEY_STATE_IDLE      = 0,
    KEY_STATE_DEBOUNCE  = 1,
    KEY_STATE_CONFIRMED = 2,
} key_state_t;

typedef struct {
    key_state_t      state;
    GPIO_TypeDef    *port;
    uint16_t         pin;
    volatile uint8_t f_push;
    volatile uint8_t f_hold;
} key_t;

extern key_t g_key[MAX_KEY_NUM];

typedef struct {
    uint8_t      button_up;
    uint8_t      button_down;
    uint8_t      button_stop;
    direction_t  direction;
} key_command_t;
extern key_command_t g_command;

void Key_Init(void);
void Key_Scan(void);
void Key_Jog_Release_Check(void);
void Key_Jog_Start_Check(void);
void Key_Jog_Conflict_Check(void);

#endif
