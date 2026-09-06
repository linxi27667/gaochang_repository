#include "app_fram.h"
#include <string.h>
#include "i2c.h"

#define APP_FRAM_I2C_ADDRESS_HAL    (APP_FRAM_I2C_ADDRESS_7BIT << 1U)
#define APP_FRAM_I2C_TIMEOUT_MS     100U
#define APP_FRAM_READY_TRIALS         3U

static uint32_t g_fram_last_hal_error;

static HAL_StatusTypeDef App_Fram_Read(uint16_t address,
                                       uint8_t *data,
                                       uint16_t length);
static HAL_StatusTypeDef App_Fram_Write(uint16_t address,
                                        const uint8_t *data,
                                        uint16_t length);

app_fram_test_result_t App_Fram_Self_Test(void)
{
    static const uint8_t test_pattern[APP_FRAM_TEST_LENGTH] =
    {
        0xA5U, 0x5AU, 0x00U, 0xFFU,
        0x12U, 0x34U, 0x56U, 0x78U,
        0x87U, 0x65U, 0x43U, 0x21U,
        0x0FU, 0xF0U, 0xC3U, 0x3CU
    };
    uint8_t backup[APP_FRAM_TEST_LENGTH];
    uint8_t verify[APP_FRAM_TEST_LENGTH];
    app_fram_test_result_t result = APP_FRAM_TEST_OK;

    g_fram_last_hal_error = HAL_I2C_ERROR_NONE;

    if (HAL_I2C_IsDeviceReady(&hi2c1,
                              APP_FRAM_I2C_ADDRESS_HAL,
                              APP_FRAM_READY_TRIALS,
                              APP_FRAM_I2C_TIMEOUT_MS) != HAL_OK)
    {
        g_fram_last_hal_error = HAL_I2C_GetError(&hi2c1);
        return APP_FRAM_TEST_DEVICE_NOT_READY;
    }

    if (App_Fram_Read(APP_FRAM_TEST_ADDRESS,
                      backup,
                      APP_FRAM_TEST_LENGTH) != HAL_OK)
    {
        return APP_FRAM_TEST_BACKUP_READ_FAILED;
    }

    if (App_Fram_Write(APP_FRAM_TEST_ADDRESS,
                       test_pattern,
                       APP_FRAM_TEST_LENGTH) != HAL_OK)
    {
        result = APP_FRAM_TEST_WRITE_FAILED;
    }
    else if (App_Fram_Read(APP_FRAM_TEST_ADDRESS,
                           verify,
                           APP_FRAM_TEST_LENGTH) != HAL_OK)
    {
        result = APP_FRAM_TEST_VERIFY_READ_FAILED;
    }
    else if (memcmp(test_pattern, verify, APP_FRAM_TEST_LENGTH) != 0)
    {
        result = APP_FRAM_TEST_VERIFY_MISMATCH;
    }

    if (App_Fram_Write(APP_FRAM_TEST_ADDRESS,
                       backup,
                       APP_FRAM_TEST_LENGTH) != HAL_OK)
    {
        return APP_FRAM_TEST_RESTORE_WRITE_FAILED;
    }

    if (App_Fram_Read(APP_FRAM_TEST_ADDRESS,
                      verify,
                      APP_FRAM_TEST_LENGTH) != HAL_OK)
    {
        return APP_FRAM_TEST_RESTORE_READ_FAILED;
    }

    if (memcmp(backup, verify, APP_FRAM_TEST_LENGTH) != 0)
    {
        return APP_FRAM_TEST_RESTORE_MISMATCH;
    }

    return result;
}

uint32_t App_Fram_Get_Last_Hal_Error(void)
{
    return g_fram_last_hal_error;
}

static HAL_StatusTypeDef App_Fram_Read(uint16_t address,
                                       uint8_t *data,
                                       uint16_t length)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&hi2c1,
                              APP_FRAM_I2C_ADDRESS_HAL,
                              address,
                              I2C_MEMADD_SIZE_16BIT,
                              data,
                              length,
                              APP_FRAM_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        g_fram_last_hal_error = HAL_I2C_GetError(&hi2c1);
    }

    return status;
}

static HAL_StatusTypeDef App_Fram_Write(uint16_t address,
                                        const uint8_t *data,
                                        uint16_t length)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Write(&hi2c1,
                               APP_FRAM_I2C_ADDRESS_HAL,
                               address,
                               I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)data,
                               length,
                               APP_FRAM_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        g_fram_last_hal_error = HAL_I2C_GetError(&hi2c1);
    }

    return status;
}
