#include "app_io_map.h"
#include "stm32f1xx_hal.h"
#include "main.h"

#if IO_MAP_DEBUG == 1
#include "elog.h"
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active_level;   /* 0=光耦低有效触发时脚为低 → 归一化后为1 */
    uint16_t      debounce_ms;
} io_in_pin_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active_level;   /* 1=继电器高有效 */
} io_out_pin_t;

/* GC-Two_Pillars：按 F407 一比一
 * 电机=继电器(RELAY0)；电磁铁/下降阀=24V 输出(OUT0/OUT1) */
static io_in_pin_t s_in_map[IO_IN_MAX] = {
    /* [IO_IN_UP_BUTTON]     */ { IN0_GPIO_Port, IN0_Pin, 0, 20 },
    /* [IO_IN_DOWN_BUTTON]   */ { IN1_GPIO_Port, IN1_Pin, 0, 20 },
    /* [IO_IN_LOCK_BUTTON]   */ { IN2_GPIO_Port, IN2_Pin, 0, 20 },
    /* [IO_IN_ESTOP]         */ { IN3_GPIO_Port, IN3_Pin, 0, 20 },
    /* [IO_IN_UPPER_LIMIT]   */ { IN4_GPIO_Port, IN4_Pin, 0, 20 },
    /* [IO_IN_LOWER_LIMIT]   */ { NULL, 0, 0, 0 },
    /* [IO_IN_REFILL_BUTTON] */ { NULL, 0, 0, 0 },
    /* [IO_IN_PHOTOELECTRIC] */ { NULL, 0, 0, 0 },
    /* [IO_IN_ROTARY_SWITCH] */ { NULL, 0, 0, 0 },
};

static const char *s_in_name[IO_IN_MAX] = {
    "up", "down", "lock", "estop", "upper", "lower", "refill", "photo", "rotary"
};

static const char *s_in_pin_name[IO_IN_MAX] = {
    "IN0", "IN1", "IN2", "IN3", "IN4", "NC", "NC", "NC", "NC"
};

static io_out_pin_t s_out_map[IO_OUT_MAX] = {
    /* [IO_OUT_MOTOR]           */ { RELAY0_GPIO_Port, RELAY0_Pin, 1 }, /* 电机继电器 */
    /* [IO_OUT_DROP_VALVE]      */ { OUT1_GPIO_Port,   OUT1_Pin,   1 }, /* 下降阀 */
    /* [IO_OUT_MAIN_AIR_VALVE]  */ { OUT0_GPIO_Port,   OUT0_Pin,   1 }, /* 电磁铁 */
    /* [IO_OUT_MAIN_WORK_VALVE] */ { NULL, 0, 0 },
    /* [IO_OUT_SUB_AIR_VALVE]   */ { NULL, 0, 0 },
    /* [IO_OUT_SUB_WORK_VALVE]  */ { NULL, 0, 0 },
};

typedef struct {
    uint8_t  last_raw;
    uint8_t  stable;
    uint32_t last_change_tick;
} io_in_debounce_t;

static io_in_debounce_t s_debounce[IO_IN_MAX];
static uint8_t s_out_state[IO_OUT_MAX] = {0};

static uint8_t io_read_physical(io_in_id_t id)
{
    if ((id >= IO_IN_MAX) || (s_in_map[id].port == NULL)) {
        return 0U;
    }
    return (HAL_GPIO_ReadPin(s_in_map[id].port, s_in_map[id].pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t io_normalize(io_in_id_t id, uint8_t physical)
{
    if ((id >= IO_IN_MAX) || (s_in_map[id].port == NULL)) {
        return 0U;
    }
    return (s_in_map[id].active_level == 0U) ? (uint8_t)(physical == 0U) : physical;
}

static const char *io_state_text(uint8_t state)
{
    return state ? "active" : "inactive";
}

static uint8_t io_is_warning_input(io_in_id_t id)
{
    return (id == IO_IN_LOCK_BUTTON ||
            id == IO_IN_ESTOP ||
            id == IO_IN_UPPER_LIMIT) ? 1U : 0U;
}

static const char *io_warning_tag(io_in_id_t id)
{
    switch (id) {
        case IO_IN_UPPER_LIMIT: return "LIMIT";
        case IO_IN_ESTOP:       return "ESTOP";
        case IO_IN_LOCK_BUTTON: return "LOCK";
        default:                return "IOMAP";
    }
}

static void io_log_input_init_state(io_in_id_t id)
{
#if IO_MAP_DEBUG == 1
    if (s_in_map[id].port == NULL) {
        return;
    }
    {
        uint8_t physical = io_read_physical(id);
        uint8_t normalized = io_normalize(id, physical);
        if ((normalized != 0U) && (io_is_warning_input(id) != 0U)) {
            const char *tag = io_warning_tag(id);
            elog_w(tag, "[%s] triggered at boot source=%s pin=%s norm=%u phy=%u",
                   tag, s_in_name[id], s_in_pin_name[id],
                   (unsigned int)normalized, (unsigned int)physical);
        } else {
            elog_i("IOMAP", "[IOMAP] input init name=%s pin=%s state=%s",
                   s_in_name[id], s_in_pin_name[id], io_state_text(normalized));
        }
    }
#else
    (void)id;
#endif
}

static void io_init_input(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void io_init_output(GPIO_TypeDef *port, uint16_t pin, uint8_t active_level)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_PinState off_state = active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, off_state);
    GPIO_InitStruct.Pin   = pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

void App_IO_Map_Init(product_type_t type)
{
    (void)type;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    io_init_input(IN0_GPIO_Port, IN0_Pin);
    io_init_input(IN1_GPIO_Port, IN1_Pin);
    io_init_input(IN2_GPIO_Port, IN2_Pin);
    io_init_input(IN3_GPIO_Port, IN3_Pin);
    io_init_input(IN4_GPIO_Port, IN4_Pin);

    io_init_output(RELAY0_GPIO_Port, RELAY0_Pin, 1); /* 电机继电器 */
    io_init_output(OUT0_GPIO_Port, OUT0_Pin, 1);     /* 电磁铁 */
    io_init_output(OUT1_GPIO_Port, OUT1_Pin, 1);     /* 下降阀 */
    /* 未用通道保持关断 */
    io_init_output(OUT2_GPIO_Port, OUT2_Pin, 1);
    io_init_output(OUT3_GPIO_Port, OUT3_Pin, 1);
    io_init_output(RELAY1_GPIO_Port, RELAY1_Pin, 1);
    io_init_output(RELAY2_GPIO_Port, RELAY2_Pin, 1);
    io_init_output(RELAY3_GPIO_Port, RELAY3_Pin, 1);
    io_init_output(RELAY4_GPIO_Port, RELAY4_Pin, 1);
    io_init_output(RELAY5_GPIO_Port, RELAY5_Pin, 1);
    HAL_GPIO_WritePin(OUT2_GPIO_Port, OUT2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OUT3_GPIO_Port, OUT3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY1_GPIO_Port, RELAY1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY2_GPIO_Port, RELAY2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY3_GPIO_Port, RELAY3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY4_GPIO_Port, RELAY4_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY5_GPIO_Port, RELAY5_Pin, GPIO_PIN_RESET);

    for (int i = 0; i < IO_IN_MAX; i++) {
        if (s_in_map[i].port == NULL) {
            s_debounce[i].last_raw = 0;
            s_debounce[i].stable = 0;
            s_debounce[i].last_change_tick = HAL_GetTick();
            continue;
        }
        uint8_t raw = io_normalize((io_in_id_t)i, io_read_physical((io_in_id_t)i));
        s_debounce[i].last_raw = raw;
        s_debounce[i].stable = raw;
        s_debounce[i].last_change_tick = HAL_GetTick();
    }

    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        if (s_out_map[i].port != NULL) {
            GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
            HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
        }
    }

#if IO_MAP_DEBUG == 1
    elog_i("IOMAP", "[IOMAP] Double-post F103: motor=RELAY0, electromagnet=OUT0, drop=OUT1");
#endif
    for (int i = 0; i < IO_IN_MAX; i++) {
        io_log_input_init_state((io_in_id_t)i);
    }
    App_IO_LogSnapshot("boot");
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    if (id >= IO_IN_MAX) return 0;
    return io_normalize(id, io_read_physical(id));
}

void App_IO_LogSnapshot(const char *reason)
{
#if IO_MAP_DEBUG == 1
    const char *why = (reason != NULL) ? reason : "?";
    elog_i("IOMAP", "[IOMAP] snapshot reason=%s", why);
    for (int i = 0; i < IO_IN_MAX; i++) {
        if (s_in_map[i].port == NULL) continue;
        uint8_t physical = io_read_physical((io_in_id_t)i);
        uint8_t normalized = io_normalize((io_in_id_t)i, physical);
        elog_i("IOMAP", "[IOMAP] in[%d] %-8s pin=%s phy=%u norm=%u stable=%u",
               i, s_in_name[i], s_in_pin_name[i],
               (unsigned int)physical, (unsigned int)normalized,
               (unsigned int)s_debounce[i].stable);
    }
#else
    (void)reason;
#endif
}

uint8_t App_IO_Read(io_in_id_t id)
{
    if (id >= IO_IN_MAX) return 0;
    if (s_in_map[id].port == NULL) return 0;

    uint8_t raw = App_IO_Read_Raw(id);
    io_in_debounce_t *db = &s_debounce[id];
    uint32_t now = HAL_GetTick();

    if (raw != db->last_raw) {
        db->last_raw = raw;
        db->last_change_tick = now;
    } else if (raw != db->stable) {
        if ((now - db->last_change_tick) >= s_in_map[id].debounce_ms) {
            db->stable = raw;
#if IO_MAP_DEBUG == 1
            elog_i("IOMAP", "[IOMAP] switch changed name=%s pin=%s state=%s",
                   s_in_name[id], s_in_pin_name[id], io_state_text(db->stable));
#endif
        }
    }
    return db->stable;
}

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id >= IO_OUT_MAX) return;
    if (s_out_map[id].port == NULL) return;

    uint8_t v = value ? 1 : 0;
    s_out_state[id] = v;

    GPIO_PinState pin_state;
    if (s_out_map[id].active_level) {
        pin_state = v ? GPIO_PIN_SET : GPIO_PIN_RESET;
    } else {
        pin_state = v ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    HAL_GPIO_WritePin(s_out_map[id].port, s_out_map[id].pin, pin_state);
}

uint8_t App_IO_Read_Output(io_out_id_t id)
{
    if (id >= IO_OUT_MAX) return 0;
    return s_out_state[id];
}

void App_IO_All_Off(void)
{
    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        if (s_out_map[i].port == NULL) continue;
        GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
    }
#if IO_MAP_DEBUG == 1
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
#endif
}
