#ifndef APP_FRAM_H
#define APP_FRAM_H

#include <stdint.h>

#define APP_FRAM_I2C_ADDRESS_7BIT    0x50U
#define APP_FRAM_SIZE_BYTES          0x2000U   /* FM24CL64B = 8KB */
#define APP_FRAM_TEST_ADDRESS        0x1FF0U
#define APP_FRAM_TEST_LENGTH         16U

typedef enum
{
    APP_FRAM_TEST_OK = 0,
    APP_FRAM_TEST_DEVICE_NOT_READY,
    APP_FRAM_TEST_BACKUP_READ_FAILED,
    APP_FRAM_TEST_WRITE_FAILED,
    APP_FRAM_TEST_VERIFY_READ_FAILED,
    APP_FRAM_TEST_VERIFY_MISMATCH,
    APP_FRAM_TEST_RESTORE_WRITE_FAILED,
    APP_FRAM_TEST_RESTORE_READ_FAILED,
    APP_FRAM_TEST_RESTORE_MISMATCH
} app_fram_test_result_t;

app_fram_test_result_t App_Fram_Self_Test(void);
uint32_t App_Fram_Get_Last_Hal_Error(void);

/* 供 W25Q 兼容层调用：0=成功，非0=失败 */
uint8_t App_Fram_Is_Ready(void);
int App_Fram_Read(uint16_t address, uint8_t *data, uint16_t length);
int App_Fram_Write(uint16_t address, const uint8_t *data, uint16_t length);

#endif
