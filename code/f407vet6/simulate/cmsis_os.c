#include "cmsis_os.h"

void osDelay(uint32_t ticks)
{
    extern void sim_tick_advance(uint32_t ms);
    sim_tick_advance(ticks);  /* 1 tick = 1ms in our config */
}

osThreadId_t osThreadNew(void (*func)(void *), void *arg, const osThreadAttr_t *attr)
{
    (void)attr;
    func(arg);
    return (void *)1;
}

void osKernelInitialize(void) {}
void osKernelStart(void) {}
