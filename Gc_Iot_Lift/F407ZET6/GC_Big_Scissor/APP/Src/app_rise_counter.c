#include "app_rise_counter.h"

#include "app_w25qxx.h"
#include "stm32f4xx_hal.h"

typedef struct
{
    uint16_t remainder_ms[2];
    uint32_t last_tick;
    lift_role_t active_role;
    uint8_t tracking;
    uint8_t initialized;
} app_rise_counter_t;

static app_rise_counter_t s_rise_counter;

static uint8_t App_RiseCounter_RoleIndex(lift_role_t role)
{
    return (role == LIFT_ROLE_SUB) ? 1U : 0U;
}

static uint16_t App_RiseCounter_NormalizeRemainder(uint32_t remainder_ms)
{
    return (uint16_t)(remainder_ms % APP_RISE_COUNTER_INTERVAL_MS);
}

static void App_RiseCounter_AddElapsed(lift_role_t role, uint32_t elapsed_ms)
{
    uint8_t index = App_RiseCounter_RoleIndex(role);
    uint32_t remaining_to_event =
        APP_RISE_COUNTER_INTERVAL_MS - s_rise_counter.remainder_ms[index];
    uint32_t event_count;

    if (elapsed_ms < remaining_to_event)
    {
        s_rise_counter.remainder_ms[index] =
            (uint16_t)(s_rise_counter.remainder_ms[index] + elapsed_ms);
        return;
    }

    elapsed_ms -= remaining_to_event;
    event_count = 1U + (elapsed_ms / APP_RISE_COUNTER_INTERVAL_MS);
    s_rise_counter.remainder_ms[index] =
        (uint16_t)(elapsed_ms % APP_RISE_COUNTER_INTERVAL_MS);

    while (event_count > 0U)
    {
        /* Persist the remainder before the event so offline records carry it. */
        App_W25Qxx_Stats_SetRiseRemainder(role, s_rise_counter.remainder_ms[index]);
        App_W25Qxx_Stats_Inc_Up(role);
        event_count--;
    }
}

void App_RiseCounter_Init(void)
{
    s_rise_counter.remainder_ms[0] = App_RiseCounter_NormalizeRemainder(
        App_W25Qxx_Stats_GetRiseRemainder(LIFT_ROLE_MAIN));
    s_rise_counter.remainder_ms[1] = App_RiseCounter_NormalizeRemainder(
        App_W25Qxx_Stats_GetRiseRemainder(LIFT_ROLE_SUB));
    s_rise_counter.last_tick = HAL_GetTick();
    s_rise_counter.active_role = LIFT_ROLE_MAIN;
    s_rise_counter.tracking = 0U;
    s_rise_counter.initialized = 1U;
}

void App_RiseCounter_Poll(lift_state_t state, lift_role_t role)
{
    uint32_t now;
    uint32_t elapsed_ms;

    if (s_rise_counter.initialized == 0U)
    {
        App_RiseCounter_Init();
    }

    now = HAL_GetTick();

    if (state == LIFT_STATE_RISING)
    {
        if (s_rise_counter.tracking == 0U)
        {
            s_rise_counter.active_role =
                (role == LIFT_ROLE_SUB) ? LIFT_ROLE_SUB : LIFT_ROLE_MAIN;
            s_rise_counter.last_tick = now;
            s_rise_counter.tracking = 1U;
            return;
        }

        elapsed_ms = now - s_rise_counter.last_tick;
        s_rise_counter.last_tick = now;
        App_RiseCounter_AddElapsed(s_rise_counter.active_role, elapsed_ms);
        return;
    }

    if (s_rise_counter.tracking != 0U)
    {
        uint8_t index;

        elapsed_ms = now - s_rise_counter.last_tick;
        App_RiseCounter_AddElapsed(s_rise_counter.active_role, elapsed_ms);
        index = App_RiseCounter_RoleIndex(s_rise_counter.active_role);
        App_W25Qxx_Stats_SetRiseRemainder(s_rise_counter.active_role,
                                          s_rise_counter.remainder_ms[index]);
        s_rise_counter.tracking = 0U;
    }
}

uint16_t App_RiseCounter_GetRemainderMs(lift_role_t role)
{
    return s_rise_counter.remainder_ms[App_RiseCounter_RoleIndex(role)];
}

void App_RiseCounter_Reset(void)
{
    s_rise_counter.remainder_ms[0] = 0U;
    s_rise_counter.remainder_ms[1] = 0U;
    s_rise_counter.last_tick = HAL_GetTick();
    s_rise_counter.tracking = 0U;
}
