#include "fake_w25qxx.h"
#include "app_w25qxx.h"

static fake_stats_t s_stats;

void fake_w25qxx_reset(void)
{
    s_stats = (fake_stats_t){0};
}

const fake_stats_t *fake_w25qxx_stats(void)
{
    return &s_stats;
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    (void)role;
    s_stats.up++;
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    (void)role;
    s_stats.down++;
}

void App_W25Qxx_Stats_Inc_Lock(void)
{
    s_stats.lock++;
}

void App_W25Qxx_Stats_Inc_Refill(void)
{
    s_stats.refill++;
}

void App_W25Qxx_Stats_Inc_Estop(void)
{
    s_stats.estop++;
}

void App_W25Qxx_Stats_Inc_PhotoAlarm(void)
{
    s_stats.photo_alarm++;
}
