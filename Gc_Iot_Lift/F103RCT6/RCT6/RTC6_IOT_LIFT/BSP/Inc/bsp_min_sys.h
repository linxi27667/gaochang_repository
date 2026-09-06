#ifndef BSP_MIN_SYS_H
#define BSP_MIN_SYS_H

#include <stdint.h>

#define BSP_MIN_SYS_INPUT_COUNT          10U
#define BSP_MIN_SYS_OUTPUT_COUNT          4U
#define BSP_MIN_SYS_RELAY_COUNT           6U
#define BSP_MIN_SYS_ADC_FULL_SCALE_MV  12111UL
#define BSP_MIN_SYS_ADC_MAX_RAW          4095UL

typedef uint8_t (*bsp_min_sys_read_input_fn)(uint8_t index);
typedef uint16_t (*bsp_min_sys_read_adc_fn)(void);
typedef void (*bsp_min_sys_write_channel_fn)(uint8_t index, uint8_t active);
typedef void (*bsp_min_sys_led_fn)(uint8_t active);
typedef void (*bsp_min_sys_led_toggle_fn)(void);

typedef struct
{
    bsp_min_sys_read_input_fn Read_Input;
    bsp_min_sys_read_adc_fn Read_Adc_Raw;
    bsp_min_sys_write_channel_fn Write_Output;
    bsp_min_sys_write_channel_fn Write_Relay;
    bsp_min_sys_led_fn Write_Led;
    bsp_min_sys_led_toggle_fn Toggle_Led;
} bsp_min_sys_device_t;

typedef struct
{
    uint16_t input_bitmap;
    uint16_t adc_raw;
    uint32_t adc_12v_mv;
    uint32_t sample_count;
} bsp_min_sys_snapshot_t;

uint8_t Bsp_MinSys_Init(bsp_min_sys_device_t *device);
uint8_t Bsp_MinSys_Sample(bsp_min_sys_device_t *device,
                          bsp_min_sys_snapshot_t *snapshot);
void Bsp_MinSys_Safe_Off(bsp_min_sys_device_t *device);
uint8_t Bsp_MinSys_Set_Output(bsp_min_sys_device_t *device,
                              uint8_t index,
                              uint8_t active);
uint8_t Bsp_MinSys_Set_Relay(bsp_min_sys_device_t *device,
                             uint8_t index,
                             uint8_t active);
void Bsp_MinSys_Set_Led(bsp_min_sys_device_t *device, uint8_t active);
void Bsp_MinSys_Toggle_Led(bsp_min_sys_device_t *device);

#endif
