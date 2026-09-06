#include "bsp_min_sys.h"

uint8_t Bsp_MinSys_Init(bsp_min_sys_device_t *device)
{
    if ((device == 0) ||
        (device->Read_Input == 0) ||
        (device->Read_Adc_Raw == 0) ||
        (device->Write_Output == 0) ||
        (device->Write_Relay == 0) ||
        (device->Write_Led == 0) ||
        (device->Toggle_Led == 0))
    {
        return 0U;
    }

    Bsp_MinSys_Safe_Off(device);
    Bsp_MinSys_Set_Led(device, 0U);
    return 1U;
}

uint8_t Bsp_MinSys_Sample(bsp_min_sys_device_t *device,
                          bsp_min_sys_snapshot_t *snapshot)
{
    uint8_t index;
    uint16_t bitmap = 0U;
    uint16_t adc_raw;

    if ((device == 0) || (snapshot == 0))
    {
        return 0U;
    }

    for (index = 0U; index < BSP_MIN_SYS_INPUT_COUNT; index++)
    {
        if (device->Read_Input(index) != 0U)
        {
            bitmap |= (uint16_t)(1UL << index);
        }
    }

    adc_raw = device->Read_Adc_Raw();
    snapshot->input_bitmap = bitmap;
    snapshot->adc_raw = adc_raw;
    snapshot->adc_12v_mv = ((uint32_t)adc_raw * BSP_MIN_SYS_ADC_FULL_SCALE_MV) /
                           BSP_MIN_SYS_ADC_MAX_RAW;
    snapshot->sample_count++;
    return 1U;
}

void Bsp_MinSys_Safe_Off(bsp_min_sys_device_t *device)
{
    uint8_t index;

    if (device == 0)
    {
        return;
    }

    for (index = 0U; index < BSP_MIN_SYS_OUTPUT_COUNT; index++)
    {
        device->Write_Output(index, 0U);
    }

    for (index = 0U; index < BSP_MIN_SYS_RELAY_COUNT; index++)
    {
        device->Write_Relay(index, 0U);
    }
}

uint8_t Bsp_MinSys_Set_Output(bsp_min_sys_device_t *device,
                              uint8_t index,
                              uint8_t active)
{
    if ((device == 0) || (index >= BSP_MIN_SYS_OUTPUT_COUNT))
    {
        return 0U;
    }

    device->Write_Output(index, active);
    return 1U;
}

uint8_t Bsp_MinSys_Set_Relay(bsp_min_sys_device_t *device,
                             uint8_t index,
                             uint8_t active)
{
    if ((device == 0) || (index >= BSP_MIN_SYS_RELAY_COUNT))
    {
        return 0U;
    }

    device->Write_Relay(index, active);
    return 1U;
}

void Bsp_MinSys_Set_Led(bsp_min_sys_device_t *device, uint8_t active)
{
    if (device != 0)
    {
        device->Write_Led(active);
    }
}

void Bsp_MinSys_Toggle_Led(bsp_min_sys_device_t *device)
{
    if (device != 0)
    {
        device->Toggle_Led();
    }
}
