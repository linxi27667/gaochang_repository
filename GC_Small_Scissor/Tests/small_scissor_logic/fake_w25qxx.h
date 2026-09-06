#ifndef TEST_FAKE_W25QXX_H
#define TEST_FAKE_W25QXX_H

#include <stdint.h>

typedef struct {
    uint32_t up;
    uint32_t down;
    uint32_t lock;
    uint32_t refill;
    uint32_t estop;
    uint32_t photo_alarm;
} fake_stats_t;

void fake_w25qxx_reset(void);
const fake_stats_t *fake_w25qxx_stats(void);
void fake_w25qxx_corrupt_latest(void);

#endif
