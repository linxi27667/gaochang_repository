/**
 * @file app_io_map.c
 * @brief F103RCT6 超薄小剪 GPIO 映射（光耦低有效）
 */
#include "app_io_map.h"
#include "stm32f1xx_hal.h"
#include "main.h"
#include "elog.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
    uint16_t debounce_ms;
} io_in_pin_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
} io_out_pin_t;

typedef struct {
    uint8_t last_raw;
    uint8_t stable;
    uint32_t last_change_tick;
} io_in_debounce_t;

static io_in_pin_t s_in_map[IO_IN_MAX] = {
    { IN0_GPIO_Port, IN0_Pin, 0, 20 }, /* UP */
    { IN1_GPIO_Port, IN1_Pin, 0, 20 }, /* DOWN */
    { IN2_GPIO_Port, IN2_Pin, 0, 20 }, /* LOCK */
    { IN3_GPIO_Port, IN3_Pin, 0, 20 }, /* ESTOP */
    { IN5_GPIO_Port, IN5_Pin, 0, 20 }, /* UPPER */
    { IN6_GPIO_Port, IN6_Pin, 0, 20 }, /* LOWER */
    { IN4_GPIO_Port, IN4_Pin, 0, 20 }, /* REFILL */
    { IN7_GPIO_Port, IN7_Pin, 0, 50 }, /* PHOTO */
};

static const char *s_in_name[IO_IN_MAX] = {
    "up", "down", "lock", "estop", "upper", "lower", "refill", "photo",
};

static const char *s_in_pin_name[IO_IN_MAX] = {
    "PA1", "PA2", "PA3", "PA4", "PA6", "PA7", "PA5", "PC4",
};

static io_out_pin_t s_out_map[IO_OUT_MAX] = {
    { RELAY0_GPIO_Port, RELAY0_Pin, 1 }, /* MOTOR 电机继电器 */
    { OUT0_GPIO_Port,   OUT0_Pin,   1 }, /* DROP  下降阀 */
    { OUT1_GPIO_Port,   OUT1_Pin,   1 }, /* AIR   气阀 */
};

static const char *s_out_name[IO_OUT_MAX] = {
    "motor", "drop_valve", "air_valve",
};

static const char *s_out_pin_name[IO_OUT_MAX] = {
    "PB12(RELAY0)", "PC3(OUT0)", "PC2(OUT1)",
};

static io_in_debounce_t s_debounce[IO_IN_MAX];
static uint8_t s_out_state[IO_OUT_MAX] = {0};

static uint8_t io_read_physical(io_in_id_t id)
{
    return (HAL_GPIO_ReadPin(s_in_map[id].port, s_in_map[id].pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t io_normalize(io_in_id_t id, uint8_t physical)
{
    return (s_in_map[id].active_level == 0U) ? (uint8_t)(physical == 0U) : physical;
}

static void io_force_unused_outputs_off(void)
{
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
    (void)type;

    for (int i = 0; i < IO_IN_MAX; i++) {
        uint8_t raw = io_normalize((io_in_id_t)i, io_read_physical((io_in_id_t)i));
        s_debounce[i].last_raw = raw;
        s_debounce[i].stable = raw;
        s_debounce[i].last_change_tick = HAL_GetTick();
    }

    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, GPIO_PIN_RESET);
    }
    io_force_unused_outputs_off();

    elog_i("IOMAP", "[IOMAP] Init done, product_type=%d", type);
    App_IO_LogSnapshot("boot");
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    if (id >= IO_IN_MAX) {
        return 0;
    }
    return io_normalize(id, io_read_physical(id));
}

void App_IO_LogSnapshot(const char *reason)
{
    const char *why = (reason != NULL) ? reason : "?";
    elog_i("IOMAP", "[IOMAP] snapshot reason=%s", why);
    for (int i = 0; i < IO_IN_MAX; i++) {
        uint8_t physical = io_read_physical((io_in_id_t)i);
        uint8_t normalized = io_normalize((io_in_id_t)i, physical);
        elog_i("IOMAP", "[IOMAP] in[%02d] %-10s pin=%s phy=%u norm=%u stable=%u",
               i, s_in_name[i], s_in_pin_name[i],
               (unsigned int)physical, (unsigned int)normalized,
               (unsigned int)s_debounce[i].stable);
    }
    for (int i = 0; i < IO_OUT_MAX; i++) {
        elog_i("IOMAP", "[IOMAP] out[%02d] %-10s pin=%s logic=%u",
               i, s_out_name[i], s_out_pin_name[i], (unsigned int)s_out_state[i]);
    }
}

uint8_t App_IO_Read(io_in_id_t id)
{
    if (id >= IO_IN_MAX) {
        return 0;
    }

    uint8_t raw = App_IO_Read_Raw(id);
    io_in_debounce_t *db = &s_debounce[id];
    uint32_t now = HAL_GetTick();

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

void App_IO_PollInputs(void)
{
    for (int i = 0; i < IO_IN_MAX; i++) {
        (void)App_IO_Read((io_in_id_t)i);
    }
}

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id >= IO_OUT_MAX) {
        return;
    }

    uint8_t v = value ? 1 : 0;
    s_out_state[id] = v;
    GPIO_PinState pin_state = s_out_map[id].active_level
                              ? (v ? GPIO_PIN_SET : GPIO_PIN_RESET)
                              : (v ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(s_out_map[id].port, s_out_map[id].pin, pin_state);
}

uint8_t App_IO_Read_Output(io_out_id_t id)
{
    if (id >= IO_OUT_MAX) {
        return 0;
    }
    return s_out_state[id];
}

void App_IO_All_Off(void)
{
    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, GPIO_PIN_RESET);
    }
    io_force_unused_outputs_off();
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
}
