/**
 * @file app_io_map.c
 * @brief F103RCT6 小剪 GPIO 映射（光耦低有效）
 */
#include "app_io_map.h"
#include "main.h"
#include "stm32f1xx_hal.h"

#if IO_MAP_DEBUG == 1
#include "elog.h"
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;  /* 0=active-low */
    uint16_t debounce_ms;
} io_in_pin_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;  /* 1=high-active relay */
} io_out_pin_t;

typedef struct {
    uint8_t last_raw;
    uint8_t stable;
    uint32_t last_change_tick;
} io_in_debounce_t;

/* 光耦低有效：脚低=触发，App_IO_Read 归一化为 1 */
static const io_in_pin_t s_in_map[IO_IN_MAX] = {
    [IO_IN_UP_BUTTON] = { IN0_GPIO_Port, IN0_Pin, 0U, 20U },
    [IO_IN_DOWN_BUTTON] = { IN1_GPIO_Port, IN1_Pin, 0U, 20U },
    [IO_IN_LOCK_BUTTON] = { IN2_GPIO_Port, IN2_Pin, 0U, 20U },
    [IO_IN_ESTOP] = { IN3_GPIO_Port, IN3_Pin, 0U, 20U },
    [IO_IN_UPPER_LIMIT] = { IN5_GPIO_Port, IN5_Pin, 0U, 20U },
    [IO_IN_REFILL_BUTTON] = { IN4_GPIO_Port, IN4_Pin, 0U, 20U },
    [IO_IN_PHOTOELECTRIC] = { IN7_GPIO_Port, IN7_Pin, 0U, 50U },
    [IO_IN_LOWER_LIMIT] = { IN6_GPIO_Port, IN6_Pin, 0U, 20U },
};

/* 电机=继电器；气阀/下降阀=24V OUT（对齐 F407：电机继电器 + 电磁阀） */
static const io_out_pin_t s_out_map[IO_OUT_MAX] = {
    [IO_OUT_MOTOR] = { RELAY0_GPIO_Port, RELAY0_Pin, 1U },
    [IO_OUT_DROP_VALVE] = { OUT0_GPIO_Port, OUT0_Pin, 1U },
    [IO_OUT_AIR_VALVE] = { OUT1_GPIO_Port, OUT1_Pin, 1U },
};

static const char *s_in_name[IO_IN_MAX] = {
    "up", "down", "lock", "estop", "upper", "refill", "photo", "lower",
};

static const char *s_in_pin_name[IO_IN_MAX] = {
    "PA1", "PA2", "PA3", "PA4", "PA6", "PA5", "PC4", "PA7",
};

static const io_af_info_t s_af_map[IO_AF_MAX] = {
    [IO_AF_USART1_TX] = { "USART1_TX", "PA9", 0U },
    [IO_AF_USART1_RX] = { "USART1_RX", "PA10", 0U },
};

static io_in_debounce_t s_debounce[IO_IN_MAX];
static uint8_t s_out_state[IO_OUT_MAX];
static uint8_t s_soft_led[IO_LED_MAX];

static uint8_t io_read_physical(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t io_normalize_input(io_in_id_t id, uint8_t physical_level)
{
    return (s_in_map[id].active_level == 0U) ? (uint8_t)(physical_level == 0U) : physical_level;
}

static GPIO_PinState io_output_state(uint8_t active_level, uint8_t value)
{
    uint8_t active = value ? 1U : 0U;
    if (active_level != 0U) {
        return active ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    return active ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

static void io_force_unused_outputs_off(void)
{
    /* 未用：RELAY1-5、OUT2-3 保持关断（OUT0/OUT1 为气阀/下降阀） */
    HAL_GPIO_WritePin(RELAY1_GPIO_Port, RELAY1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY2_GPIO_Port, RELAY2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY3_GPIO_Port, RELAY3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY4_GPIO_Port, RELAY4_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY5_GPIO_Port, RELAY5_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OUT2_GPIO_Port, OUT2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OUT3_GPIO_Port, OUT3_Pin, GPIO_PIN_RESET);
}

void App_IO_Map_Init(product_type_t type)
{
    g_product_type = type;
    g_current_role = LIFT_ROLE_MAIN;

    App_IO_All_Off();
    io_force_unused_outputs_off();
    App_IO_LedOff(IO_LED_RUN);
    App_IO_LedOff(IO_LED_COM);
    App_IO_LedOff(IO_LED_POWER);

    for (uint32_t i = 0U; i < IO_IN_MAX; i++) {
        uint8_t raw = io_normalize_input((io_in_id_t)i,
            io_read_physical(s_in_map[i].port, s_in_map[i].pin));
        s_debounce[i].last_raw = raw;
        s_debounce[i].stable = raw;
        s_debounce[i].last_change_tick = HAL_GetTick();
    }

#if IO_MAP_DEBUG == 1
    elog_i("IOMAP", "[IOMAP] Init done, product_type=%d", type);
#endif
    App_IO_LogSnapshot("boot");
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    if (id >= IO_IN_MAX) {
        return 0U;
    }
    return io_read_physical(s_in_map[id].port, s_in_map[id].pin);
}

void App_IO_LogSnapshot(const char *reason)
{
#if IO_MAP_DEBUG == 1
    const char *why = (reason != NULL) ? reason : "?";
    elog_i("IOMAP", "[IOMAP] snapshot reason=%s", why);
    for (uint32_t i = 0U; i < IO_IN_MAX; i++) {
        io_in_id_t id = (io_in_id_t)i;
        uint8_t physical = App_IO_Read_Raw(id);
        uint8_t normalized = io_normalize_input(id, physical);
        elog_i("IOMAP", "[IOMAP] in[%02d] %-10s pin=%s phy=%u norm=%u stable=%u",
               (int)i, s_in_name[id], s_in_pin_name[id],
               (unsigned int)physical, (unsigned int)normalized,
               (unsigned int)s_debounce[id].stable);
    }
#else
    (void)reason;
#endif
}

uint8_t App_IO_Read(io_in_id_t id)
{
    io_in_debounce_t *db;
    uint8_t raw;
    uint32_t now;

    if (id >= IO_IN_MAX) {
        return 0U;
    }

    raw = io_normalize_input(id, App_IO_Read_Raw(id));
    db = &s_debounce[id];
    now = HAL_GetTick();

    if (raw != db->last_raw) {
        db->last_raw = raw;
        db->last_change_tick = now;
    } else if (raw != db->stable) {
        if ((now - db->last_change_tick) >= s_in_map[id].debounce_ms) {
            db->stable = raw;
        }
    }
    return db->stable;
}

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id >= IO_OUT_MAX) {
        return;
    }
    s_out_state[id] = value ? 1U : 0U;
    HAL_GPIO_WritePin(s_out_map[id].port, s_out_map[id].pin,
                      io_output_state(s_out_map[id].active_level, s_out_state[id]));
}

uint8_t App_IO_Read_Output(io_out_id_t id)
{
    if (id >= IO_OUT_MAX) {
        return 0U;
    }
    return s_out_state[id];
}

void App_IO_All_Off(void)
{
    for (uint32_t i = 0U; i < IO_OUT_MAX; i++) {
        App_IO_Write((io_out_id_t)i, 0U);
    }
    io_force_unused_outputs_off();
#if IO_MAP_DEBUG == 1
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
#endif
}

void App_IO_PlatformWrite(io_platform_id_t id, uint8_t level)
{
    if (id == IO_PLATFORM_LED_RUN) {
        HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin,
                          level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

uint8_t App_IO_PlatformRead(io_platform_id_t id)
{
    if (id == IO_PLATFORM_LED_RUN) {
        return io_read_physical(LED_RUN_GPIO_Port, LED_RUN_Pin);
    }
    return 0U;
}

void App_IO_W25Q_Select(void) {}
void App_IO_W25Q_Deselect(void) {}
void App_IO_RS485_SetTx(void) {}
void App_IO_RS485_SetRx(void) {}

void App_IO_LedOn(io_led_id_t id)
{
    if (id >= IO_LED_MAX) {
        return;
    }
    s_soft_led[id] = 1U;
    if (id == IO_LED_RUN) {
        LED_RUN_ON();
    }
}

void App_IO_LedOff(io_led_id_t id)
{
    if (id >= IO_LED_MAX) {
        return;
    }
    s_soft_led[id] = 0U;
    if (id == IO_LED_RUN) {
        LED_RUN_OFF();
    }
}

void App_IO_LedToggle(io_led_id_t id)
{
    if (id >= IO_LED_MAX) {
        return;
    }
    if (s_soft_led[id] != 0U) {
        App_IO_LedOff(id);
    } else {
        App_IO_LedOn(id);
    }
}

const io_af_info_t *App_IO_GetAFInfo(io_af_id_t id)
{
    if (id >= IO_AF_MAX) {
        return NULL;
    }
    return &s_af_map[id];
}
