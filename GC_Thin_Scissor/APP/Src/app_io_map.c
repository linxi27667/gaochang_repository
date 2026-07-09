#include "app_io_map.h"
#include "stm32f4xx_hal.h"
#include "main.h"

#include "elog.h"

/* ============ 寮曡剼鏄犲皠琛?============ */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active_level;   /* 0=active-low (鎸変笅/瑙﹀彂鏃惰 0)锛?=active-high (瑙﹀彂鏃惰 1) */
    uint16_t      debounce_ms;    /* 鍘绘姈鏃堕棿 */
} io_in_pin_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active_level;   /* 杈撳嚭鏋佹€э細1=楂樼數骞冲惛鍚堬紙榛樿锛夛紝0=浣庣數骞冲惛鍚?*/
} io_out_pin_t;

/* Thin scissor IO map follows the large-scissor common connector. */
static io_in_pin_t s_in_map[IO_IN_MAX] = {
    /* [IO_IN_UP_BUTTON]     */ { BTN_UP_GPIO_Port,     BTN_UP_Pin,     0, 20 },
    /* [IO_IN_DOWN_BUTTON]   */ { BTN_DOWN_GPIO_Port,   BTN_DOWN_Pin,   0, 20 },
    /* [IO_IN_LOCK_BUTTON]   */ { BTN_LOCK_GPIO_Port,   BTN_LOCK_Pin,   0, 20 },
    /* [IO_IN_ESTOP]         */ { ESTOP_NC_GPIO_Port,   ESTOP_NC_Pin,   0, 20 },
    /* [IO_IN_UPPER_LIMIT]   */ { LIMIT_UP_GPIO_Port,   LIMIT_UP_Pin,   1, 20 },
    /* [IO_IN_LOWER_LIMIT]   */ { LIMIT_DOWN_GPIO_Port, LIMIT_DOWN_Pin, 1, 20 },
    /* [IO_IN_REFILL_BUTTON] */ { BTN_REFILL_GPIO_Port, BTN_REFILL_Pin, 0, 20 },
    /* [IO_IN_PHOTOELECTRIC] */ { PHOTO_EYE_GPIO_Port,  PHOTO_EYE_Pin,  1, 50 },
};

static const char *s_in_name[IO_IN_MAX] = {
    "up",
    "down",
    "lock",
    "estop",
    "upper",
    "lower",
    "refill",
    "photo",
};

static const char *s_in_pin_name[IO_IN_MAX] = {
    "PG15",
    "PE0",
    "PE1",
    "PE2",
    "PE5",
    "PE6",
    "PE3",
    "PE8",
};

static io_out_pin_t s_out_map[IO_OUT_MAX] = {
    /* [IO_OUT_MOTOR]          */ { OUT_MOTOR_RELAY_GPIO_Port,     OUT_MOTOR_RELAY_Pin,     1 },
    /* [IO_OUT_DROP_VALVE]     */ { OUT_DOWN_VALVE_RELAY_GPIO_Port, OUT_DOWN_VALVE_RELAY_Pin, 1 },
    /* [IO_OUT_MAIN_AIR_VALVE] */ { OUT_AIR_VALVE_GPIO_Port,       OUT_AIR_VALVE_Pin,       1 },
};

static const char *s_out_name[IO_OUT_MAX] = {
    "motor",
    "drop_valve",
    "air_valve",
};

static const char *s_out_pin_name[IO_OUT_MAX] = {
    "PF8",
    "PF9",
    "PD8",
};

/* ============ 鍘绘姈鐘舵€?============ */

typedef struct {
    uint8_t  last_raw;       /* 涓婃鍘熷璇绘暟 */
    uint8_t  stable;         /* 绋冲畾杈撳嚭鍊?*/
    uint32_t last_change_tick; /* 鍘熷璇绘暟鍙樺寲鏃跺埢 */
} io_in_debounce_t;

static io_in_debounce_t s_debounce[IO_IN_MAX];

/* ============ 杈撳嚭鐘舵€佽窡韪?============ */

static uint8_t s_out_state[IO_OUT_MAX] = {0};

static uint8_t io_read_physical(io_in_id_t id)
{
    return (HAL_GPIO_ReadPin(s_in_map[id].port, s_in_map[id].pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t io_normalize(io_in_id_t id, uint8_t physical)
{
    return (s_in_map[id].active_level == 0U) ? (uint8_t)(physical == 0U) : physical;
}

static const char *io_state_text(uint8_t state)
{
    return state ? "active" : "inactive";
}

static uint8_t io_is_warning_input(io_in_id_t id)
{
    return (id < IO_IN_MAX) ? 1U : 0U;
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
        case IO_IN_UP_BUTTON:
        case IO_IN_DOWN_BUTTON:
        case IO_IN_LOCK_BUTTON:
        case IO_IN_REFILL_BUTTON:
            return "KEY";
        default:
            return "IOMAP";
    }
}

static void io_log_input_init_state(io_in_id_t id)
{
    uint8_t physical = io_read_physical(id);
    uint8_t normalized = io_normalize(id, physical);

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
}

static void io_log_output_init_state(io_out_id_t id)
{
    uint8_t logical = s_out_state[id];

    elog_i("IOMAP", "[IOMAP] output init id=%d name=%s pin=%s state=%s logic=%u active=%u",
           (int)id,
           s_out_name[id],
           s_out_pin_name[id],
           io_state_text(logical),
           (unsigned int)logical,
           (unsigned int)s_out_map[id].active_level);
}

/* ============ 鍐呴儴锛欸PIO 鍒濆鍖栵紙鍏煎鏈敤 CubeMX 閲嶆柊閰嶇疆鐨勬儏鍐碉級 ============ */

static void io_init_input(GPIO_TypeDef *port, uint16_t pin, uint8_t pullup)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = pullup ? GPIO_PULLUP : GPIO_PULLDOWN;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void io_init_output(GPIO_TypeDef *port, uint16_t pin, uint8_t active_level)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /* 鍏堝啓鏂紑鐢靛钩鍐嶉厤缃负杈撳嚭锛岄伩鍏嶄笂鐢电灛闂磋鍔ㄤ綔
     * active_level=1锛堥珮鐢靛钩鍚稿悎锛夆啋 鏂紑=RESET锛堜綆鐢靛钩锛?     * active_level=0锛堜綆鐢靛钩鍚稿悎锛夆啋 鏂紑=SET锛堥珮鐢靛钩锛?     */
    GPIO_PinState off_state = active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, off_state);
    GPIO_InitStruct.Pin   = pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

/* ============ 瀵瑰鎺ュ彛瀹炵幇 ============ */

void App_IO_Map_Init(product_type_t type)
{
    (void)type;  /* 鐩墠浠呭疄鐜板ぇ鍓槧灏勶紝鍏朵粬鍨嬪彿棰勭暀 */

    /* 浣胯兘鎵€鏈夌敤鍒扮殑 GPIO 鏃堕挓 */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();  /* PF8/PF9 鏂板 */

    io_init_input(BTN_UP_GPIO_Port, BTN_UP_Pin, 1);
    io_init_input(BTN_DOWN_GPIO_Port, BTN_DOWN_Pin, 1);
    io_init_input(BTN_LOCK_GPIO_Port, BTN_LOCK_Pin, 1);
    io_init_input(ESTOP_NC_GPIO_Port, ESTOP_NC_Pin, 1);
    io_init_input(LIMIT_UP_GPIO_Port, LIMIT_UP_Pin, 1);
    io_init_input(LIMIT_DOWN_GPIO_Port, LIMIT_DOWN_Pin, 1);
    io_init_input(BTN_REFILL_GPIO_Port, BTN_REFILL_Pin, 1);
    io_init_input(PHOTO_EYE_GPIO_Port, PHOTO_EYE_Pin, 1);

    io_init_output(OUT_MOTOR_RELAY_GPIO_Port, OUT_MOTOR_RELAY_Pin, 1);
    io_init_output(OUT_DOWN_VALVE_RELAY_GPIO_Port, OUT_DOWN_VALVE_RELAY_Pin, 1);
    io_init_output(OUT_AIR_VALVE_GPIO_Port, OUT_AIR_VALVE_Pin, 1);

    /* 鍒濆鍖栧幓鎶栫姸鎬?*/
    for (int i = 0; i < IO_IN_MAX; i++) {
        uint8_t raw = io_normalize((io_in_id_t)i, io_read_physical((io_in_id_t)i));
        s_debounce[i].last_raw          = raw;
        s_debounce[i].stable            = raw;
        s_debounce[i].last_change_tick  = HAL_GetTick();
    }

    /* 鍒濆鍖栬緭鍑虹姸鎬佷负鍏ㄦ柇寮€锛堟寜鍚勫紩鑴?active_level 鍐欐柇寮€鐢靛钩锛?*/
    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        /* active_level=1锛堥珮鐢靛钩鍚稿悎锛夆啋 鏂紑=RESET锛堜綆鐢靛钩锛?         * active_level=0锛堜綆鐢靛钩鍚稿悎锛夆啋 鏂紑=SET锛堥珮鐢靛钩锛?         */
        GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
    }

    elog_i("IOMAP", "[IOMAP] Init done, product_type=%d", type);
    uint8_t boot_inhibit = 0U;
    for (int i = 0; i < IO_IN_MAX; i++) {
        if (s_debounce[i].stable != 0U) {
            boot_inhibit = 1U;
        }
        io_log_input_init_state((io_in_id_t)i);
    }
    for (int i = 0; i < IO_OUT_MAX; i++) {
        io_log_output_init_state((io_out_id_t)i);
    }
    if (boot_inhibit != 0U) {
        App_IO_All_Off();
        elog_w("IOMAP", "[IOMAP] boot input active, motion inhibited, outputs off");
    }
    App_IO_LogSnapshot("boot");
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    if (id >= IO_IN_MAX) return 0;
    uint8_t raw = io_read_physical(id);
    /* 褰掍竴鍖栵細active-low 鏃跺彇鍙?*/
    return io_normalize(id, raw);
}

void App_IO_LogSnapshot(const char *reason)
{
    const char *why = (reason != NULL) ? reason : "?";

    elog_i("IOMAP", "[IOMAP] snapshot reason=%s", why);
    for (int i = 0; i < IO_IN_MAX; i++) {
        io_in_id_t id = (io_in_id_t)i;
        uint8_t physical = io_read_physical(id);
        uint8_t normalized = io_normalize(id, physical);

        if ((normalized != 0U) && (io_is_warning_input(id) != 0U)) {
            const char *tag = io_warning_tag(id);
            elog_w(tag, "[%s] snapshot active reason=%s source=%s id=%d pin=%s phy=%u norm=%u stable=%u active=%u debounce=%u",
                   tag,
                   why,
                   s_in_name[i],
                   i,
                   s_in_pin_name[i],
                   (unsigned int)physical,
                   (unsigned int)normalized,
                   (unsigned int)s_debounce[i].stable,
                   (unsigned int)s_in_map[i].active_level,
                   (unsigned int)s_in_map[i].debounce_ms);
        } else {
            elog_i("IOMAP", "[IOMAP] in[%02d] %-10s pin=%s phy=%u norm=%u stable=%u active=%u debounce=%u",
                   i,
                   s_in_name[i],
                   s_in_pin_name[i],
                   (unsigned int)physical,
                   (unsigned int)normalized,
                   (unsigned int)s_debounce[i].stable,
                   (unsigned int)s_in_map[i].active_level,
                   (unsigned int)s_in_map[i].debounce_ms);
        }
    }
    for (int i = 0; i < IO_OUT_MAX; i++) {
        elog_i("IOMAP", "[IOMAP] out[%02d] %-10s pin=%s logic=%u active=%u",
               i,
               s_out_name[i],
               s_out_pin_name[i],
               (unsigned int)s_out_state[i],
               (unsigned int)s_out_map[i].active_level);
    }
}

uint8_t App_IO_Read(io_in_id_t id)
{
    if (id >= IO_IN_MAX) return 0;

    uint8_t raw = App_IO_Read_Raw(id);
    io_in_debounce_t *db = &s_debounce[id];
    uint32_t now = HAL_GetTick();

    if (raw != db->last_raw) {
        db->last_raw         = raw;
        db->last_change_tick = now;
    } else if (raw != db->stable) {
        /* 鍘熷鍊煎凡绋冲畾瓒呰繃鍘绘姈鏃堕棿锛屾洿鏂扮ǔ瀹氬€?*/
        if ((now - db->last_change_tick) >= s_in_map[id].debounce_ms) {
            uint8_t old = db->stable;
            db->stable = raw;
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
                       (unsigned int)io_read_physical(id),
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
                       (unsigned int)io_read_physical(id),
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
                       (unsigned int)io_read_physical(id),
                       (unsigned int)s_in_map[id].active_level,
                       (unsigned int)s_in_map[id].debounce_ms);
            }
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
    if (id >= IO_OUT_MAX) return;

    uint8_t v = value ? 1 : 0;
    s_out_state[id] = v;

    /* 鎸?active_level 杞崲閫昏緫鍊煎埌鐗╃悊鐢靛钩
     * active_level=1锛堥珮鐢靛钩鍚稿悎锛夛細value=1 鈫?SET锛寁alue=0 鈫?RESET
     * active_level=0锛堜綆鐢靛钩鍚稿悎锛夛細value=1 鈫?RESET锛寁alue=0 鈫?SET
     */
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
        /* 鎸?active_level 鍐欐柇寮€鐢靛钩 */
        GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
    }
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
}

