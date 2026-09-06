#ifndef __LIFT_CORE_H__
#define __LIFT_CORE_H__

#include <stdint.h>

typedef enum {
    LIFT_STATE_IDLE = 0,
    LIFT_STATE_RISING = 1,
    LIFT_STATE_DROPPING = 2,
    LIFT_STATE_LOCKED = 3,
    LIFT_STATE_REFILLING = 4,
    LIFT_STATE_ESTOP = 5,
    LIFT_STATE_PHOTO_ALARM = 6
} lift_state_t;

typedef struct {
    void (*init)(void);
    void (*on_up_pressed)(void);
    void (*on_down_pressed)(void);
    void (*on_lock_pressed)(void);
    void (*on_refill_pressed)(void);
    void (*on_estop)(void);
    void (*on_photoelectric_blocked)(void);
    void (*on_clear_alarm)(void);
    void (*poll)(void);
} lift_ops_t;

extern volatile lift_state_t g_lift_state;
extern const lift_ops_t *g_lift_ops;
extern volatile uint8_t s_remote_locked;

void LiftCore_Init(void);
void LiftCore_Poll(void);
void LiftCore_ClearAlarm(void);
void LiftCore_SetRemoteLock(uint8_t locked);
const char *LiftCore_StateName(lift_state_t state);

#endif
