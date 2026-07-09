#include "app_io_map.h"
#include "stm32f4xx_hal.h"
#include "main.h"

#if IO_MAP_DEBUG == 1
#include "elog.h"
#endif

/* ============ 引脚映射表 ============ */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active_level;   /* 0=active-low (按下/触发时读 0)，1=active-high (触发时读 1) */
    uint16_t      debounce_ms;    /* 去抖时间 */
} io_in_pin_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active_level;   /* 输出极性：1=高电平吸合（默认），0=低电平吸合 */
} io_out_pin_t;

/* 大剪引脚映射表
 * 输入电平假设（待硬件确认）：
 *   - 按钮 (UP/DOWN/LOCK/REFILL): active-low，按下=0V，上拉输入
 *   - 急停 ESTOP: active-low，默认上拉为 1，急停触发时拉低读 0
 *   - 上限位 UPPER_LIMIT: NC 常闭，触发时断开 → 上拉输入，触发时读 1
 *   - 光电 PHOTOELECTRIC: 遮挡时输出高 → 上拉输入，遮挡时读 1
 *   - 旋转开关 ROTARY: 高电平=主机，低电平=子机
 */
static io_in_pin_t s_in_map[IO_IN_MAX] = {
    /* [IO_IN_UP_BUTTON]     */ { GPIOG, GPIO_PIN_15, 0, 20 }, /* PG15 上升按钮 active-low */
    /* [IO_IN_DOWN_BUTTON]   */ { GPIOE, GPIO_PIN_0, 0, 20 },  /* PE0 下降按钮 active-low */
    /* [IO_IN_LOCK_BUTTON]   */ { GPIOE, GPIO_PIN_1, 0, 20 },  /* PE1 锁定按钮 active-low */
    /* [IO_IN_ESTOP]         */ { GPIOE, GPIO_PIN_2, 0, 20 },  /* PE2 急停 active-low */
    /* [IO_IN_UPPER_LIMIT]   */ { GPIOE, GPIO_PIN_5, 1, 20 },  /* PE5 主机上限位 active-high */
    /* [IO_IN_LOWER_LIMIT]   */ { GPIOE, GPIO_PIN_6, 1, 20 },  /* PE6 主机下限位 active-high（NC 常闭，触发读 1） */
    /* [IO_IN_REFILL_BUTTON] */ { GPIOE, GPIO_PIN_3, 0, 20 },  /* PE3 补油按钮 active-low */
    /* [IO_IN_PHOTOELECTRIC] */ { GPIOE, GPIO_PIN_8, 1, 50 },  /* PE8 光电 active-high */
    /* [IO_IN_ROTARY_SWITCH] */ { GPIOE, GPIO_PIN_4, 1, 50 },  /* PE4 旋转开关 active-high */
    /* [IO_IN_SUB_UPPER_LIMIT]*/ { GPIOE, GPIO_PIN_7, 1, 20 }, /* PE7 子机上限位 active-high（NC 常闭，触发读 1） */
    { GPIOE, GPIO_PIN_12, 0, 20 }, /* IO_IN_SUB_LOWER_LIMIT: unused, keep inactive with pull-up */
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
    "rotary",
    "sub_upper",
    "sub_lower",
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
    "PE4",
    "PE7",
    "PE12",
};

static io_out_pin_t s_out_map[IO_OUT_MAX] = {
    /* [IO_OUT_MOTOR]           */ { GPIOF, GPIO_PIN_8,  1 },  /* PF8 电机继电器 高电平吸合 */
    /* [IO_OUT_DROP_VALVE]      */ { GPIOF, GPIO_PIN_9,  1 },  /* PF9 下降阀 高电平吸合 */
    /* [IO_OUT_MAIN_AIR_VALVE]  */ { GPIOD, GPIO_PIN_8,  1 },  /* PD8 主机气阀 高电平吸合 */
    /* [IO_OUT_MAIN_WORK_VALVE] */ { GPIOD, GPIO_PIN_9,  1 },  /* PD9 主机工作阀 高电平吸合 */
    /* [IO_OUT_SUB_AIR_VALVE]   */ { GPIOD, GPIO_PIN_10, 1 },  /* PD10 子机气阀 高电平吸合 */
    /* [IO_OUT_SUB_WORK_VALVE]  */ { GPIOD, GPIO_PIN_11, 1 },  /* PD11 子机工作阀 高电平吸合 */
};

/* ============ 去抖状态 ============ */

typedef struct {
    uint8_t  last_raw;       /* 上次原始读数 */
    uint8_t  stable;         /* 稳定输出值 */
    uint32_t last_change_tick; /* 原始读数变化时刻 */
} io_in_debounce_t;

static io_in_debounce_t s_debounce[IO_IN_MAX];

/* ============ 输出状态跟踪 ============ */

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
    return (id == IO_IN_LOCK_BUTTON ||
            id == IO_IN_ESTOP ||
            id == IO_IN_UPPER_LIMIT ||
            id == IO_IN_LOWER_LIMIT ||
            id == IO_IN_PHOTOELECTRIC ||
            id == IO_IN_SUB_UPPER_LIMIT ||
            id == IO_IN_SUB_LOWER_LIMIT) ? 1U : 0U;
}

static const char *io_warning_tag(io_in_id_t id)
{
    switch (id) {
        case IO_IN_UPPER_LIMIT:
        case IO_IN_LOWER_LIMIT:
        case IO_IN_SUB_UPPER_LIMIT:
        case IO_IN_SUB_LOWER_LIMIT:
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
#else
    (void)id;
#endif
}

/* ============ 内部：GPIO 初始化（兼容未用 CubeMX 重新配置的情况） ============ */

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
    /* 先写断开电平再配置为输出，避免上电瞬间误动作
     * active_level=1（高电平吸合）→ 断开=RESET（低电平）
     * active_level=0（低电平吸合）→ 断开=SET（高电平）
     */
    GPIO_PinState off_state = active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, off_state);
    GPIO_InitStruct.Pin   = pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

/* ============ 对外接口实现 ============ */

void App_IO_Map_Init(product_type_t type)
{
    (void)type;  /* 目前仅实现大剪映射，其他型号预留 */

    /* 使能所有用到的 GPIO 时钟 */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();  /* PF8/PF9 新增 */

    /* 初始化输入引脚（上拉输入）
     * PG15/PE0-PE8 需要初始化为上拉输入。
     */
    io_init_input(GPIOG, GPIO_PIN_15, 1); /* PG15 上升按钮 上拉 */
    io_init_input(GPIOE, GPIO_PIN_0, 1);  /* PE0 下降按钮 上拉 */
    io_init_input(GPIOE, GPIO_PIN_1, 1);  /* PE1 锁定按钮 上拉 */
    io_init_input(GPIOE, GPIO_PIN_2, 1);  /* PE2 急停 上拉 */
    io_init_input(GPIOE, GPIO_PIN_3, 1);  /* PE3 补油按钮 上拉 */
    io_init_input(GPIOE, GPIO_PIN_4, 1);  /* PE4 旋转开关 上拉 */
    io_init_input(GPIOE, GPIO_PIN_5, 1);  /* PE5 主机上限位 上拉 */
    io_init_input(GPIOE, GPIO_PIN_6, 1);  /* PE6 主机下限位 上拉 */
    io_init_input(GPIOE, GPIO_PIN_7, 1);  /* PE7 子机上限位 上拉 */
    io_init_input(GPIOE, GPIO_PIN_8, 1);  /* PE8 光电 上拉 */
    io_init_input(GPIOE, GPIO_PIN_12, 1); /* PE12 unused sub lower, pull-up */

    /* 初始化输出引脚
     * PD8-PD11 已由 CubeMX 配置为推挽输出（双柱的电机/方向继电器）
     * PF8/PF9 需要新增初始化
     */
    io_init_output(GPIOF, GPIO_PIN_8, 1);  /* PF8 电机 高电平吸合 */
    io_init_output(GPIOF, GPIO_PIN_9, 1);  /* PF9 下降阀 高电平吸合 */

    /* 初始化去抖状态 */
    for (int i = 0; i < IO_IN_MAX; i++) {
        uint8_t raw = io_normalize((io_in_id_t)i, io_read_physical((io_in_id_t)i));
        s_debounce[i].last_raw          = raw;
        s_debounce[i].stable            = raw;
        s_debounce[i].last_change_tick  = HAL_GetTick();
    }

    /* 初始化输出状态为全断开（按各引脚 active_level 写断开电平） */
    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        /* active_level=1（高电平吸合）→ 断开=RESET（低电平）
         * active_level=0（低电平吸合）→ 断开=SET（高电平）
         */
        GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
    }

#if IO_MAP_DEBUG == 1
    elog_i("IOMAP", "[IOMAP] Init done, product_type=%d", type);
#endif
    for (int i = 0; i < IO_IN_MAX; i++) {
        io_log_input_init_state((io_in_id_t)i);
    }
    App_IO_LogSnapshot("boot");
}

uint8_t App_IO_Read_Raw(io_in_id_t id)
{
    if (id >= IO_IN_MAX) return 0;
    uint8_t raw = io_read_physical(id);
    /* 归一化：active-low 时取反 */
    return io_normalize(id, raw);
}

void App_IO_LogSnapshot(const char *reason)
{
#if IO_MAP_DEBUG == 1
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
#else
    (void)reason;
#endif
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
        /* 原始值已稳定超过去抖时间，更新稳定值 */
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
#endif
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

    /* 按 active_level 转换逻辑值到物理电平
     * active_level=1（高电平吸合）：value=1 → SET，value=0 → RESET
     * active_level=0（低电平吸合）：value=1 → RESET，value=0 → SET
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
        /* 按 active_level 写断开电平 */
        GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
    }
#if IO_MAP_DEBUG == 1
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
#endif
}
