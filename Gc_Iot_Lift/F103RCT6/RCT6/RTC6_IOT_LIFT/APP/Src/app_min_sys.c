#include "app_min_sys.h"
#include "adc.h"
#include "main.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} min_sys_gpio_t;

static const min_sys_gpio_t g_inputs[BSP_MIN_SYS_INPUT_COUNT] =
{
    {IN0_GPIO_Port, IN0_Pin},
    {IN1_GPIO_Port, IN1_Pin},
    {IN2_GPIO_Port, IN2_Pin},
    {IN3_GPIO_Port, IN3_Pin},
    {IN4_GPIO_Port, IN4_Pin},
    {IN5_GPIO_Port, IN5_Pin},
    {IN6_GPIO_Port, IN6_Pin},
    {IN7_GPIO_Port, IN7_Pin},
    {IN8_GPIO_Port, IN8_Pin},
    {IN9_GPIO_Port, IN9_Pin}
};

static const min_sys_gpio_t g_outputs[BSP_MIN_SYS_OUTPUT_COUNT] =
{
    {OUT0_GPIO_Port, OUT0_Pin},
    {OUT1_GPIO_Port, OUT1_Pin},
    {OUT2_GPIO_Port, OUT2_Pin},
    {OUT3_GPIO_Port, OUT3_Pin}
};

static const min_sys_gpio_t g_relays[BSP_MIN_SYS_RELAY_COUNT] =
{
    {RELAY0_GPIO_Port, RELAY0_Pin},
    {RELAY1_GPIO_Port, RELAY1_Pin},
    {RELAY2_GPIO_Port, RELAY2_Pin},
    {RELAY3_GPIO_Port, RELAY3_Pin},
    {RELAY4_GPIO_Port, RELAY4_Pin},
    {RELAY5_GPIO_Port, RELAY5_Pin}
};

static bsp_min_sys_snapshot_t g_min_sys_snapshot;
static uint8_t g_adc_ready;

static uint8_t HW_Read_Input(uint8_t index);
static uint16_t HW_Read_Adc_Raw(void);
static void HW_Write_Output(uint8_t index, uint8_t active);
static void HW_Write_Relay(uint8_t index, uint8_t active);
static void HW_Write_Led(uint8_t active);
static void HW_Toggle_Led(void);
static GPIO_PinState HW_Get_Output_State(uint8_t active);

static bsp_min_sys_device_t g_min_sys_device =
{
    HW_Read_Input,
    HW_Read_Adc_Raw,
    HW_Write_Output,
    HW_Write_Relay,
    HW_Write_Led,
    HW_Toggle_Led
};

static uint8_t HW_Read_Input(uint8_t index)
{
    if (index >= BSP_MIN_SYS_INPUT_COUNT)
    {
        return 0U;
    }

    /* 光耦导通时 MCU 侧被拉低，因此低电平表示输入有效。 */
    return (HAL_GPIO_ReadPin(g_inputs[index].port, g_inputs[index].pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static uint16_t HW_Read_Adc_Raw(void)
{
    uint16_t value = 0U;

    if (g_adc_ready == 0U)
    {
        return 0U;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 2U) == HAL_OK)
    {
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }

    (void)HAL_ADC_Stop(&hadc1);
    return value;
}

static void HW_Write_Output(uint8_t index, uint8_t active)
{
    if (index < BSP_MIN_SYS_OUTPUT_COUNT)
    {
        HAL_GPIO_WritePin(g_outputs[index].port,
                          g_outputs[index].pin,
                          HW_Get_Output_State(active));
    }
}

static void HW_Write_Relay(uint8_t index, uint8_t active)
{
    if (index < BSP_MIN_SYS_RELAY_COUNT)
    {
        HAL_GPIO_WritePin(g_relays[index].port,
                          g_relays[index].pin,
                          HW_Get_Output_State(active));
    }
}

static void HW_Write_Led(uint8_t active)
{
    HAL_GPIO_WritePin(LED_RUN_GPIO_Port,
                      LED_RUN_Pin,
                      HW_Get_Output_State(active));
}

static void HW_Toggle_Led(void)
{
    HAL_GPIO_TogglePin(LED_RUN_GPIO_Port, LED_RUN_Pin);
}

static GPIO_PinState HW_Get_Output_State(uint8_t active)
{
    return (active != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

uint8_t App_MinSys_Init(void)
{
    MinSys_Safe_Off();
    g_adc_ready = (HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK) ? 1U : 0U;
    return Bsp_MinSys_Init(&g_min_sys_device);
}

uint8_t MinSys_Sample(void)
{
    return Bsp_MinSys_Sample(&g_min_sys_device, &g_min_sys_snapshot);
}

void MinSys_Get_Snapshot(bsp_min_sys_snapshot_t *snapshot)
{
    if (snapshot != 0)
    {
        *snapshot = g_min_sys_snapshot;
    }
}

void MinSys_Safe_Off(void)
{
    Bsp_MinSys_Safe_Off(&g_min_sys_device);
}

uint8_t MinSys_Set_Output(uint8_t index, uint8_t active)
{
    return Bsp_MinSys_Set_Output(&g_min_sys_device, index, active);
}

uint8_t MinSys_Set_Relay(uint8_t index, uint8_t active)
{
    return Bsp_MinSys_Set_Relay(&g_min_sys_device, index, active);
}

void MinSys_Set_Run_Led(uint8_t active)
{
    Bsp_MinSys_Set_Led(&g_min_sys_device, active);
}

void MinSys_Toggle_Run_Led(void)
{
    Bsp_MinSys_Toggle_Led(&g_min_sys_device);
}

uint8_t MinSys_Is_Adc_Ready(void)
{
    return g_adc_ready;
}
