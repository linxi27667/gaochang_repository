#ifndef SIM_CMSIS_OS_H
#define SIM_CMSIS_OS_H

#include <stdint.h>

/* Delay in RTOS ticks */
void osDelay(uint32_t ticks);

/* Thread creation */
typedef void *osThreadId_t;

typedef enum {
    osPriorityNone          = 0,
    osPriorityIdle          = 1,
    osPriorityLow           = 8,
    osPriorityNormal        = 24,
    osPriorityAboveNormal   = 32,
    osPriorityHigh          = 40,
    osPriorityRealtime      = 48,
} osPriority_t;

typedef struct {
    const char    *name;
    uint32_t       stack_size;
    osPriority_t   priority;
} osThreadAttr_t;

osThreadId_t osThreadNew(void (*func)(void *), void *arg, const osThreadAttr_t *attr);
void osKernelInitialize(void);
void osKernelStart(void);

#endif
