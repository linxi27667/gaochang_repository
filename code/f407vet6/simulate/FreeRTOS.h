#ifndef SIM_FREERTOS_H
#define SIM_FREERTOS_H

#include <stdint.h>

typedef void (*TaskFunction_t)(void *);

#define tskIDLE_PRIORITY    0
#define configMAX_PRIORITIES 56

#endif
