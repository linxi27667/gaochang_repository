#include "fake_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "app_product.h"
#include "app_spi.h"
#include "app_w25qxx.h"
#include "lift_iot.h"

#define FAKE_FLASH_SIZE 0x00024000U
#define FAKE_SECTOR_SIZE 0x00001000U

static uint8_t s_flash[FAKE_FLASH_SIZE];
static uint8_t s_input[IO_IN_MAX];
static uint8_t s_output[IO_OUT_MAX];
static uint32_t s_tick;

product_type_t g_product_type = PRODUCT_TYPE_THIN_SCISSOR;
lift_role_t g_current_role = LIFT_ROLE_MAIN;
volatile lift_state_t g_lift_state = LIFT_STATE_IDLE;
const lift_ops_t *g_lift_ops = NULL;
volatile uint8_t s_remote_locked = 0U;
spi_bus_t SPI_Bus = {0};

static uint8_t fake_flash_range(uint32_t addr, uint32_t len)
{
    return ((addr <= FAKE_FLASH_SIZE) && (len <= (FAKE_FLASH_SIZE - addr))) ? 1U : 0U;
}

void fake_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    memset(s_input, 0, sizeof(s_input));
    memset(s_output, 0, sizeof(s_output));
    s_tick = 0U;
    g_product_type = PRODUCT_TYPE_THIN_SCISSOR;
    g_current_role = LIFT_ROLE_MAIN;
    g_lift_state = LIFT_STATE_IDLE;
    s_remote_locked = 0U;
    App_W25Qxx_System_Init();
    LiftIot_Init();
}

void fake_reboot(void)
{
    memset(s_input, 0, sizeof(s_input));
    memset(s_output, 0, sizeof(s_output));
    g_product_type = PRODUCT_TYPE_THIN_SCISSOR;
    g_current_role = LIFT_ROLE_MAIN;
    g_lift_state = LIFT_STATE_IDLE;
    s_remote_locked = 0U;
    App_W25Qxx_System_Init();
    LiftIot_Init();
}

void fake_advance_ms(uint32_t ms)
{
    s_tick += ms;
}

void fake_set_state(lift_state_t state)
{
    g_lift_state = state;
}

void fake_set_input(io_in_id_t id, uint8_t active)
{
    if (id < IO_IN_MAX)
    {
        s_input[id] = active ? 1U : 0U;
    }
}

uint8_t fake_output(io_out_id_t id)
{
    return (id < IO_OUT_MAX) ? s_output[id] : 0U;
}

int fake_map_chip_uid(void)
{
    const uintptr_t base = 0x1FFF0000UL;
    const uintptr_t uid_addr = 0x1FFF7A10UL;
    uint8_t *mapped;
    uint32_t *uid;

    mapped = (uint8_t *)VirtualAlloc((void *)base, 0x10000U,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if ((mapped == NULL) || ((uintptr_t)mapped > uid_addr) ||
        (((uintptr_t)mapped + 0x10000U) < (uid_addr + 12U)))
    {
        return 0;
    }

    uid = (uint32_t *)uid_addr;
    uid[0] = 0x11223344U;
    uid[1] = 0x55667788U;
    uid[2] = 0x99AABBCCU;
    return 1;
}

void fake_corrupt_flash(uint32_t addr)
{
    if (addr < FAKE_FLASH_SIZE)
    {
        s_flash[addr] ^= 0x01U;
    }
}

uint32_t HAL_GetTick(void)
{
    return s_tick;
}

uint8_t App_IO_Read(io_in_id_t id)
{
    return (id < IO_IN_MAX) ? s_input[id] : 0U;
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    return App_IO_Read(id);
}

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id < IO_OUT_MAX)
    {
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

void App_IO_Map_Init(int type)
{
    (void)type;
}

void App_IO_PollInputs(void)
{
}

void App_IO_LogSnapshot(const char *reason)
{
    (void)reason;
}

void LiftLock_Init(void)
{
}

void LiftLock_LockState(void)
{
}

void LiftLock_UnlockState(void)
{
}

void LiftLock_LockIot(void)
{
}

void LiftLock_UnlockIot(void)
{
}

void LiftCore_Init(void)
{
}

void LiftCore_Poll(void)
{
}

void LiftCore_ClearAlarm(void)
{
}

void LiftCore_SetRemoteLock(uint8_t locked)
{
    s_remote_locked = locked ? 1U : 0U;
    if (s_remote_locked != 0U)
    {
        g_lift_state = LIFT_STATE_IDLE;
    }
}

const char *LiftCore_StateName(lift_state_t state)
{
    (void)state;
    return "test";
}

const char *App_Product_TypeName(product_type_t type)
{
    (void)type;
    return "thin_scissor";
}

const char *App_Product_RoleName(lift_role_t role)
{
    (void)role;
    return "main";
}

void App_Product_PrintUID(void)
{
}

uint16_t App_OpLog_Count(void)
{
    return 0U;
}

uint8_t App_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    (void)index;
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }
    return W25Q_ERR;
}

void App_OpLog_Clear(void)
{
}

void App_SPI_System_Init(void)
{
}

void SPI_Bus_Init(spi_bus_t *bus)
{
    (void)bus;
}

void SPI_Bus_Select(spi_bus_t *bus)
{
    (void)bus;
}

void SPI_Bus_Deselect(spi_bus_t *bus)
{
    (void)bus;
}

uint8_t W25Q_Init_Device(w25q_t *flash)
{
    return (flash != NULL) ? W25Q_OK : W25Q_ERR;
}

uint32_t W25Q_Read_JEDEC_ID(w25q_t *flash)
{
    (void)flash;
    return 0xEF4018U;
}

uint16_t W25Q_Read_Device_ID(w25q_t *flash)
{
    (void)flash;
    return 0x4018U;
}

uint8_t W25Q_Read_Byte(w25q_t *flash, uint32_t addr)
{
    (void)flash;
    return (addr < FAKE_FLASH_SIZE) ? s_flash[addr] : 0xFFU;
}

uint8_t W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len)
{
    (void)flash;
    if ((buf == NULL) || (fake_flash_range(addr, len) == 0U))
    {
        return W25Q_ERR;
    }

    memcpy(buf, &s_flash[addr], len);
    return W25Q_OK;
}

uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr)
{
    (void)flash;
    if (((addr % FAKE_SECTOR_SIZE) != 0U) ||
        (fake_flash_range(addr, FAKE_SECTOR_SIZE) == 0U))
    {
        return W25Q_ERR;
    }

    memset(&s_flash[addr], 0xFF, FAKE_SECTOR_SIZE);
    return W25Q_OK;
}

uint8_t W25Q_Chip_Erase(w25q_t *flash)
{
    (void)flash;
    memset(s_flash, 0xFF, sizeof(s_flash));
    return W25Q_OK;
}

uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    return W25Q_Write_MultiPage(flash, addr, data, len);
}

uint8_t W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    (void)flash;
    if ((data == NULL) || (fake_flash_range(addr, len) == 0U))
    {
        return W25Q_ERR;
    }

    for (i = 0U; i < len; ++i)
    {
        s_flash[addr + i] &= data[i];
    }

    return W25Q_OK;
}
