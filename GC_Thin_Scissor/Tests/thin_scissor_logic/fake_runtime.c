#include "fake_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_op_log.h"
#include "app_product.h"
#include "app_w25qxx.h"
#include "lift_core.h"

product_type_t g_product_type = PRODUCT_TYPE_THIN_SCISSOR;
lift_role_t g_current_role = LIFT_ROLE_MAIN;
app_config_t g_config = {
    PRODUCT_TYPE_THIN_SCISSOR,
    200U,
    3000U,
};

static uint32_t s_tick;
static uint8_t s_input[IO_IN_MAX];
static uint8_t s_output[IO_OUT_MAX];
static char s_log[8192];
static char s_last_op[64];

static void append_log(const char *tag, const char *fmt, va_list ap)
{
    size_t used = strlen(s_log);
    if (used >= sizeof(s_log) - 1U) {
        return;
    }

    int n = snprintf(&s_log[used], sizeof(s_log) - used, "[%s] ", tag);
    if (n < 0) {
        return;
    }
    used += (size_t)n;
    if (used >= sizeof(s_log) - 1U) {
        return;
    }
    vsnprintf(&s_log[used], sizeof(s_log) - used, fmt, ap);
    strncat(s_log, "\n", sizeof(s_log) - strlen(s_log) - 1U);
}

void fake_reset(void)
{
    s_tick = 0U;
    memset(s_output, 0, sizeof(s_output));
    memset(s_input, 0, sizeof(s_input));
    memset(s_log, 0, sizeof(s_log));
    memset(s_last_op, 0, sizeof(s_last_op));
    g_lift_state = LIFT_STATE_IDLE;
    g_product_type = PRODUCT_TYPE_THIN_SCISSOR;
    g_current_role = LIFT_ROLE_MAIN;
    g_config.product_type = PRODUCT_TYPE_THIN_SCISSOR;
    g_config.motor_to_valve_delay_ms = 200U;
    g_config.motor_hold_ms = 3000U;
}

void fake_advance_ms(uint32_t ms)
{
    s_tick += ms;
}

uint32_t HAL_GetTick(void)
{
    return s_tick;
}

void fake_set_input(io_in_id_t id, uint8_t active)
{
    if (id < IO_IN_MAX) {
        s_input[id] = active ? 1U : 0U;
    }
}

uint8_t App_IO_Read(io_in_id_t id)
{
    return (id < IO_IN_MAX) ? s_input[id] : 0U;
}

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id < IO_OUT_MAX) {
        s_output[id] = value ? 1U : 0U;
    }
}

uint8_t App_IO_Read_Output(io_out_id_t id)
{
    return fake_output(id);
}

void App_IO_All_Off(void)
{
    memset(s_output, 0, sizeof(s_output));
}

void App_IO_LogSnapshot(const char *reason)
{
    (void)reason;
}

uint8_t fake_output(io_out_id_t id)
{
    return (id < IO_OUT_MAX) ? s_output[id] : 0U;
}

int fake_log_contains(const char *needle)
{
    return strstr(s_log, needle) != NULL;
}

void elog_i(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    append_log(tag, fmt, ap);
    va_end(ap);
}

void elog_w(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    append_log(tag, fmt, ap);
    va_end(ap);
}

void elog_e(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    append_log(tag, fmt, ap);
    va_end(ap);
}

int App_OpLog_Record(op_type_t op, op_result_t result, uint16_t duration_s,
                     const void *extra, uint8_t extra_len)
{
    (void)result;
    (void)duration_s;
    (void)extra;
    (void)extra_len;
    snprintf(s_last_op, sizeof(s_last_op), "op=%d", (int)op);
    return 0;
}

const char *fake_op_log_last(void)
{
    return s_last_op;
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role) { (void)role; }
void App_W25Qxx_Stats_Inc_Down(lift_role_t role) { (void)role; }
void App_W25Qxx_Stats_Inc_Lock(void) { }
void App_W25Qxx_Stats_Inc_Refill(void) { }
void App_W25Qxx_Stats_Inc_Estop(void) { }
void App_W25Qxx_Stats_Inc_PhotoAlarm(void) { }

void LiftLock_Init(void) { }
void LiftLock_LockState(void) { }
void LiftLock_UnlockState(void) { }
void LiftLock_LockIot(void) { }
void LiftLock_UnlockIot(void) { }

void LiftIot_NotifyPhotoAlarm(void) { }
void LiftIot_NotifyEstop(void) { }

const char *App_Product_TypeName(product_type_t type)
{
    return (type == PRODUCT_TYPE_THIN_SCISSOR) ? "thin_scissor" : "unknown";
}
