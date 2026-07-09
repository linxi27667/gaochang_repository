/**
 * @file app_io_map.c
 * @brief GPIO abstraction for GC Small Scissor APP code.
 *
 * Converts physical GPIO levels into active input/output semantics, keeps the
 * platform GPIO helpers together, and centralizes all logical IO names.
 */
#include "app_io_map.h"

#include "main.h"
#include "stm32f4xx_hal.h"

#if IO_MAP_DEBUG == 1
#include "elog.h"
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;  /* 0=active-low, 1=active-high */
    uint16_t debounce_ms;
} io_in_pin_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;  /* 0=low-active output, 1=high-active output */
} io_out_pin_t;

typedef struct {
    uint8_t last_raw;
    uint8_t stable;
    uint32_t last_change_tick;
} io_in_debounce_t;

/* 灏忓壀杈撳叆锛氬叏閮ㄧ敱 CubeMX 閰嶆垚 GPIO_Input + Pull-up銆? * active_level=0 琛ㄧず鎸変笅/瑙﹀彂鏃剁墿鐞嗚 0锛孉pp_IO_Read() 杩斿洖 1銆? * active_level=1 琛ㄧず瑙﹀彂鏃剁墿鐞嗚 1锛孉pp_IO_Read() 杩斿洖 1銆? */
static const io_in_pin_t s_in_map[IO_IN_MAX] = {
    [IO_IN_UP_BUTTON] = { BTN_UP_GPIO_Port, BTN_UP_Pin, 0U, 20U },          /* PE0 涓婂崌鎸夐挳锛屾寜涓嬫帴鍦帮紝active-low */
    [IO_IN_DOWN_BUTTON] = { BTN_DOWN_GPIO_Port, BTN_DOWN_Pin, 0U, 20U },    /* PE1 涓嬮檷鎸夐挳锛屾寜涓嬫帴鍦帮紝active-low */
    [IO_IN_LOCK_BUTTON] = { BTN_LOCK_GPIO_Port, BTN_LOCK_Pin, 0U, 20U },    /* PE2 閿佸畾鎸夐挳锛屾寜涓嬫帴鍦帮紝active-low */
    [IO_IN_ESTOP] = { ESTOP_NC_GPIO_Port, ESTOP_NC_Pin, 0U, 20U },          /* PE3 鎬ュ仠 NC锛屾柇寮€/鎬ュ仠璇?1锛宎ctive-high */
    [IO_IN_UPPER_LIMIT] = { LIMIT_UP_GPIO_Port, LIMIT_UP_Pin, 1U, 20U },    /* PE4 upper limit: default high, touched pulls low */
    [IO_IN_REFILL_BUTTON] = { BTN_REFILL_GPIO_Port, BTN_REFILL_Pin, 0U, 20U }, /* PE6 琛ユ补鎸夐挳锛屾寜涓嬫帴鍦帮紝active-low */
    [IO_IN_PHOTOELECTRIC] = { PHOTO_EYE_GPIO_Port, PHOTO_EYE_Pin, 1U, 50U }, /* PE7 鍏夌數锛岄伄鎸℃帴鍦帮紝active-low */
    [IO_IN_LOWER_LIMIT] = { GPIOE, GPIO_PIN_6, 1U, 20U }, /* PE6 lower limit, active-high; masks photo only */
};

/* 灏忓壀鍔ㄤ綔杈撳嚭锛氬叏閮ㄧ敱 CubeMX 閰嶆垚 GPIO_Output PP + Pull-down + 榛樿 Reset銆? * active_level=1 琛ㄧず App_IO_Write(id, 1) 杈撳嚭楂樼數骞筹紝缁х數鍣?闃€鍔ㄤ綔銆? */
static const io_out_pin_t s_out_map[IO_OUT_MAX] = {
    [IO_OUT_MOTOR] = { OUT_MOTOR_RELAY_GPIO_Port, OUT_MOTOR_RELAY_Pin, 1U },              /* PF8 鐢垫満缁х數鍣紝楂樼數骞冲惛鍚?*/
    [IO_OUT_DROP_VALVE] = { OUT_DOWN_VALVE_RELAY_GPIO_Port, OUT_DOWN_VALVE_RELAY_Pin, 1U }, /* PF9 涓嬮檷闃€缁х數鍣紝楂樼數骞冲惛鍚?*/
    [IO_OUT_AIR_VALVE] = { OUT_AIR_VALVE_GPIO_Port, OUT_AIR_VALVE_Pin, 1U },              /* PD8 姘旈榾杈撳嚭锛岄珮鐢靛钩鎵撳紑 */
};

/* 骞冲彴 GPIO锛氫笉鍖呭惈 SPI/UART 澶嶇敤鑴氬垵濮嬪寲锛屽彧绠＄悊鏅€?GPIO 鐨勭數骞冲姩浣溿€?*/
static const io_out_pin_t s_platform_map[IO_PLATFORM_MAX] = {
    [IO_PLATFORM_W25Q_CS] = { W25Q_CS_GPIO_Port, W25Q_CS_Pin, 0U },              /* PB6 W25Q_CS锛岄粯璁?Set锛岄珮鐢靛钩涓嶉€変腑锛屼綆鐢靛钩閫変腑 */
    [IO_PLATFORM_RS485_DIR] = { RS485_Uart3_Dir_GPIO_Port, RS485_Uart3_Dir_Pin, 1U }, /* PD0 RS485 DIR锛岄粯璁?Reset锛屼綆鎺ユ敹锛岄珮鍙戦€?*/
    [IO_PLATFORM_LED_RUN] = { Led_Run_GPIO_Port, Led_Run_Pin, 0U },             /* PG2 Run LED锛岄粯璁?Set锛屼綆鐢靛钩鐐逛寒 */
    [IO_PLATFORM_LED_COM] = { Led_Com_GPIO_Port, Led_Com_Pin, 0U },             /* PG3 Com LED锛岄粯璁?Set锛屼綆鐢靛钩鐐逛寒 */
    [IO_PLATFORM_LED_POWER] = { Led_Power_GPIO_Port, Led_Power_Pin, 0U },       /* PG4 Power LED锛岄粯璁?Set锛屼綆鐢靛钩鐐逛寒 */
};

static const io_platform_id_t s_led_to_platform[IO_LED_MAX] = {
    [IO_LED_RUN] = IO_PLATFORM_LED_RUN,
    [IO_LED_COM] = IO_PLATFORM_LED_COM,
    [IO_LED_POWER] = IO_PLATFORM_LED_POWER,
};

static const char *s_in_name[IO_IN_MAX] = {
    "up",
    "down",
    "lock",
    "estop",
    "upper",
    "refill",
    "photo",
    "lower",
};

static const char *s_in_pin_name[IO_IN_MAX] = {
    "PG15",
    "PE0",
    "PE1",
    "PE2",
    "PE5",
    "PE3",
    "PE8",
    "PE6",
};

/* 澶嶇敤鍔熻兘寮曡剼鐧昏琛細杩欓噷鍙褰曠敤閫旓紝瀹為檯 GPIO AF 鍒濆鍖栫敱 CubeMX 鐢熸垚浠ｇ爜瀹屾垚銆?*/
static const io_af_info_t s_af_map[IO_AF_MAX] = {
    [IO_AF_SPI1_SCK] = { "SPI1_SCK", "PB3", 5U },       /* PB3 SPI1_SCK锛孉F5锛孨o Pull锛孷ery High speed */
    [IO_AF_SPI1_MISO] = { "SPI1_MISO", "PB4", 5U },     /* PB4 SPI1_MISO锛孉F5锛孨o Pull锛孷ery High speed */
    [IO_AF_SPI1_MOSI] = { "SPI1_MOSI", "PB5", 5U },     /* PB5 SPI1_MOSI锛孉F5锛孨o Pull锛孷ery High speed */
    [IO_AF_USART3_TX] = { "USART3_TX", "PB10", 7U },    /* PB10 USART3_TX锛孉F7锛孨o Pull锛?600 */
    [IO_AF_USART3_RX] = { "USART3_RX", "PB11", 7U },    /* PB11 USART3_RX锛孉F7锛孭ull-up锛孌MA RX circular */
    [IO_AF_USART6_TX] = { "USART6_TX", "PC6", 8U },     /* PC6 USART6_TX锛孉F8锛孨o Pull锛?15200 */
    [IO_AF_USART6_RX] = { "USART6_RX", "PC7", 8U },     /* PC7 USART6_RX锛孉F8锛孨o Pull锛孌MA RX circular */
};

static io_in_debounce_t s_debounce[IO_IN_MAX];
static uint8_t s_out_state[IO_OUT_MAX];

static uint8_t io_read_physical(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t io_normalize_input(io_in_id_t id, uint8_t physical_level)
{
    return (s_in_map[id].active_level == 0U) ? (uint8_t)(physical_level == 0U) : physical_level;
}

static const char *io_state_text(uint8_t state)
{
    return state ? "active" : "inactive";
}

static uint8_t io_is_warning_input(io_in_id_t id)
{
    return (uint8_t)((id == IO_IN_LOCK_BUTTON) ||
                     (id == IO_IN_ESTOP) ||
                     (id == IO_IN_UPPER_LIMIT) ||
                     (id == IO_IN_LOWER_LIMIT) ||
                     (id == IO_IN_PHOTOELECTRIC));
}

static const char *io_warning_tag(io_in_id_t id)
{
    switch (id) {
    case IO_IN_UPPER_LIMIT:
    case IO_IN_LOWER_LIMIT:
        return "LIMIT";
    case IO_IN_ESTOP:
        return "ESTOP";
    case IO_IN_PHOTOELECTRIC:
        return "PHOTO";
    case IO_IN_LOCK_BUTTON:
        return "LOCK";
    default:
        return "IOMAP";
    }
}

static void io_log_input_init_state(io_in_id_t id)
{
#if IO_MAP_DEBUG == 1
    uint8_t physical = io_read_physical(s_in_map[id].port, s_in_map[id].pin);
    uint8_t normalized = io_normalize_input(id, physical);

    if ((normalized != 0U) && (io_is_warning_input(id) != 0U)) {
        const char *tag = io_warning_tag(id);
        elog_w(tag, "[%s] triggered at boot source=%s id=%d pin=%s state=%s norm=%u phy=%u active=%u debounce=%u",
               tag,
               s_in_name[id],
               (int)id,
               s_in_pin_name[id],
               io_state_text(normalized),
               (unsigned int)normalized,
               (unsigned int)physical,
               (unsigned int)s_in_map[id].active_level,
               (unsigned int)s_in_map[id].debounce_ms);
    } else {
        elog_i("IOMAP", "[IOMAP] input init id=%d name=%s pin=%s state=%s norm=%u phy=%u active=%u debounce=%u",
               (int)id,
               s_in_name[id],
               s_in_pin_name[id],
               io_state_text(normalized),
               (unsigned int)normalized,
               (unsigned int)physical,
               (unsigned int)s_in_map[id].active_level,
               (unsigned int)s_in_map[id].debounce_ms);
    }
#else
    (void)id;
#endif
}

static GPIO_PinState io_output_state(uint8_t active_level, uint8_t value)
{
    uint8_t active = value ? 1U : 0U;

    if (active_level != 0U) {
        return active ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }

    return active ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

void App_IO_Map_Init(product_type_t type)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    g_product_type = type;
    g_current_role = LIFT_ROLE_MAIN;

    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    App_IO_All_Off();
    App_IO_W25Q_Deselect();
    App_IO_RS485_SetRx();
    App_IO_LedOff(IO_LED_RUN);
    App_IO_LedOff(IO_LED_COM);
    App_IO_LedOff(IO_LED_POWER);

    for (uint32_t i = 0U; i < IO_IN_MAX; i++) {
        uint8_t raw = io_normalize_input((io_in_id_t)i, io_read_physical(s_in_map[i].port, s_in_map[i].pin));
        s_debounce[i].last_raw = raw;
        s_debounce[i].stable = raw;
        s_debounce[i].last_change_tick = HAL_GetTick();
    }

#if IO_MAP_DEBUG == 1
    elog_i("IOMAP", "[IOMAP] Init done, product_type=%d", type);
#endif
    for (uint32_t i = 0U; i < IO_IN_MAX; i++) {
        io_log_input_init_state((io_in_id_t)i);
    }
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
        uint8_t physical = io_read_physical(s_in_map[id].port, s_in_map[id].pin);
        uint8_t normalized = io_normalize_input(id, physical);

        if ((normalized != 0U) && (io_is_warning_input(id) != 0U)) {
            const char *tag = io_warning_tag(id);
            elog_w(tag, "[%s] snapshot active reason=%s source=%s id=%d pin=%s phy=%u norm=%u stable=%u active=%u debounce=%u",
                   tag,
                   why,
                   s_in_name[id],
                   (int)id,
                   s_in_pin_name[id],
                   (unsigned int)physical,
                   (unsigned int)normalized,
                   (unsigned int)s_debounce[id].stable,
                   (unsigned int)s_in_map[id].active_level,
                   (unsigned int)s_in_map[id].debounce_ms);
        } else {
            elog_i("IOMAP", "[IOMAP] in[%02d] %-10s pin=%s phy=%u norm=%u stable=%u active=%u debounce=%u",
                   (int)i,
                   s_in_name[id],
                   s_in_pin_name[id],
                   (unsigned int)physical,
                   (unsigned int)normalized,
                   (unsigned int)s_debounce[id].stable,
                   (unsigned int)s_in_map[id].active_level,
                   (unsigned int)s_in_map[id].debounce_ms);
        }
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
            uint8_t old = db->stable;
            db->stable = raw;
#if IO_MAP_DEBUG == 1
            if ((db->stable != 0U) && (io_is_warning_input(id) != 0U)) {
                const char *tag = io_warning_tag(id);
                elog_w(tag, "[%s] triggered source=%s id=%d pin=%s state=%s %u->%u phy=%u active=%u debounce=%u",
                       tag,
                       s_in_name[id],
                       (int)id,
                       s_in_pin_name[id],
                       io_state_text(db->stable),
                       (unsigned int)old,
                       (unsigned int)db->stable,
                       (unsigned int)App_IO_Read_Raw(id),
                       (unsigned int)s_in_map[id].active_level,
                       (unsigned int)s_in_map[id].debounce_ms);
            } else if ((old != 0U) && (db->stable == 0U) && (io_is_warning_input(id) != 0U)) {
                const char *tag = io_warning_tag(id);
                elog_i(tag, "[%s] cleared source=%s id=%d pin=%s state=%s %u->%u phy=%u active=%u debounce=%u",
                       tag,
                       s_in_name[id],
                       (int)id,
                       s_in_pin_name[id],
                       io_state_text(db->stable),
                       (unsigned int)old,
                       (unsigned int)db->stable,
                       (unsigned int)App_IO_Read_Raw(id),
                       (unsigned int)s_in_map[id].active_level,
                       (unsigned int)s_in_map[id].debounce_ms);
            } else {
                elog_i("IOMAP", "[IOMAP] switch changed id=%d name=%s pin=%s state=%s %u->%u phy=%u active=%u debounce=%u",
                       (int)id,
                       s_in_name[id],
                       s_in_pin_name[id],
                       io_state_text(db->stable),
                       (unsigned int)old,
                       (unsigned int)db->stable,
                       (unsigned int)App_IO_Read_Raw(id),
                       (unsigned int)s_in_map[id].active_level,
                       (unsigned int)s_in_map[id].debounce_ms);
            }
#endif
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
    HAL_GPIO_WritePin(s_out_map[id].port,
                      s_out_map[id].pin,
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
#if IO_MAP_DEBUG == 1
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
#endif
}

void App_IO_PlatformWrite(io_platform_id_t id, uint8_t level)
{
    if (id >= IO_PLATFORM_MAX) {
        return;
    }

    HAL_GPIO_WritePin(s_platform_map[id].port,
                      s_platform_map[id].pin,
                      level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t App_IO_PlatformRead(io_platform_id_t id)
{
    if (id >= IO_PLATFORM_MAX) {
        return 0U;
    }

    return io_read_physical(s_platform_map[id].port, s_platform_map[id].pin);
}

void App_IO_W25Q_Select(void)
{
    App_IO_PlatformWrite(IO_PLATFORM_W25Q_CS, 0U);
}

void App_IO_W25Q_Deselect(void)
{
    App_IO_PlatformWrite(IO_PLATFORM_W25Q_CS, 1U);
}

void App_IO_RS485_SetTx(void)
{
    App_IO_PlatformWrite(IO_PLATFORM_RS485_DIR, 1U);
}

void App_IO_RS485_SetRx(void)
{
    App_IO_PlatformWrite(IO_PLATFORM_RS485_DIR, 0U);
}

void App_IO_LedOn(io_led_id_t id)
{
    if (id >= IO_LED_MAX) {
        return;
    }

    App_IO_PlatformWrite(s_led_to_platform[id], 0U);
}

void App_IO_LedOff(io_led_id_t id)
{
    if (id >= IO_LED_MAX) {
        return;
    }

    App_IO_PlatformWrite(s_led_to_platform[id], 1U);
}

void App_IO_LedToggle(io_led_id_t id)
{
    io_platform_id_t platform_id;

    if (id >= IO_LED_MAX) {
        return;
    }

    platform_id = s_led_to_platform[id];
    HAL_GPIO_TogglePin(s_platform_map[platform_id].port, s_platform_map[platform_id].pin);
}

const io_af_info_t *App_IO_GetAFInfo(io_af_id_t id)
{
    if (id >= IO_AF_MAX) {
        return NULL;
    }

    return &s_af_map[id];
}
