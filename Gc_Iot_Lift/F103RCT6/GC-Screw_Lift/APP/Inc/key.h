#ifndef KEY_H
#define KEY_H

#include <stdint.h>
#include "main.h"
#include "motor.h"

/* ===== 按键引脚：PA1=上升 PA2=下降（光耦低有效）===== */
#define KEY_UP_PORT         Up_Key_GPIO_Port
#define KEY_UP_PIN          Up_Key_Pin
#define KEY_DOWN_PORT       Down_Key_GPIO_Port
#define KEY_DOWN_PIN        Down_Key_Pin

#define KEY_PRESS   0
#define KEY_RELEASE 1

#define MAX_KEY_NUM 2

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
    volatile direction_t  direction;
} key_command_t;
extern key_command_t g_command;

void Key_Init(void);
void Key_Scan(void);
void Key_Jog_Release_Check(void);
void Key_Jog_Start_Check(void);
void Key_Jog_Conflict_Check(void);

#endif
