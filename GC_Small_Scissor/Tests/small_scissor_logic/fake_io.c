#include "fake_io.h"

#include <string.h>

static uint8_t s_raw[IO_IN_MAX];
static uint8_t s_out[IO_OUT_MAX];
static uint8_t s_platform[IO_PLATFORM_MAX];

void fake_io_reset(void)
{
    memset(s_raw, 1, sizeof(s_raw));
    memset(s_out, 0, sizeof(s_out));
    memset(s_platform, 0, sizeof(s_platform));

    s_raw[IO_IN_UPPER_LIMIT] = 0U;
    s_raw[IO_IN_LOWER_LIMIT] = 0U;
    s_raw[IO_IN_PHOTOELECTRIC] = 0U;
}

void fake_io_set_raw(io_in_id_t id, uint8_t raw)
{
    if (id < IO_IN_MAX) {
        s_raw[id] = raw ? 1U : 0U;
    }
}

uint8_t fake_io_get_out(io_out_id_t id)
{
    return (id < IO_OUT_MAX) ? s_out[id] : 0U;
}

void App_IO_Map_Init(product_type_t type)
{
    (void)type;
    fake_io_reset();
}

uint8_t App_IO_Read(io_in_id_t id)
{
    if (id >= IO_IN_MAX) {
        return 0U;
    }

    if ((id == IO_IN_UPPER_LIMIT) ||
        (id == IO_IN_LOWER_LIMIT) ||
        (id == IO_IN_PHOTOELECTRIC)) {
        return s_raw[id] ? 1U : 0U;
    }

    return s_raw[id] ? 0U : 1U;
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    return (id < IO_IN_MAX) ? s_raw[id] : 0U;
}

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id < IO_OUT_MAX) {
        s_out[id] = value ? 1U : 0U;
    }
}

uint8_t App_IO_Read_Output(io_out_id_t id)
{
    return fake_io_get_out(id);
}

void App_IO_All_Off(void)
{
    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out[i] = 0U;
    }
}

void App_IO_PlatformWrite(io_platform_id_t id, uint8_t level)
{
    if (id < IO_PLATFORM_MAX) {
        s_platform[id] = level ? 1U : 0U;
    }
}

uint8_t App_IO_PlatformRead(io_platform_id_t id)
{
    return (id < IO_PLATFORM_MAX) ? s_platform[id] : 0U;
}

void App_IO_W25Q_Select(void) { }
void App_IO_W25Q_Deselect(void) { }
void App_IO_RS485_SetTx(void) { }
void App_IO_RS485_SetRx(void) { }
void App_IO_LedOn(io_led_id_t id) { (void)id; }
void App_IO_LedOff(io_led_id_t id) { (void)id; }
void App_IO_LedToggle(io_led_id_t id) { (void)id; }
const io_af_info_t *App_IO_GetAFInfo(io_af_id_t id) { (void)id; return 0; }
