#include "app_io_map.h"
#include "stm32f4xx_hal.h"
#include "main.h"

#if IO_MAP_DEBUG == 1
#include "elog.h"
#endif

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

/* 澶у壀寮曡剼鏄犲皠琛? * 杈撳叆鐢靛钩鍋囪锛堝緟纭欢纭锛夛細
 *   - 鎸夐挳 (UP/DOWN/LOCK/REFILL): active-low锛屾寜涓?0V锛屼笂鎷夎緭鍏? *   - 鎬ュ仠 ESTOP: NC 甯搁棴锛岃Е鍙戞椂鏂紑鐢佃矾 鈫?涓婃媺杈撳叆锛岃Е鍙戞椂璇?1
 *   - 涓婇檺浣?UPPER_LIMIT: NC 甯搁棴锛岃Е鍙戞椂鏂紑 鈫?涓婃媺杈撳叆锛岃Е鍙戞椂璇?1
 *   - 鍏夌數 PHOTOELECTRIC: 閬尅鏃惰緭鍑洪珮 鈫?涓婃媺杈撳叆锛岄伄鎸℃椂璇?1
 *   - 鏃嬭浆寮€鍏?ROTARY: 楂樼數骞?涓绘満锛屼綆鐢靛钩=瀛愭満
 */
static io_in_pin_t s_in_map[IO_IN_MAX] = {
    /* [IO_IN_UP_BUTTON]     */ { GPIOE, GPIO_PIN_0, 0, 20 },  /* PE0 涓婂崌鎸夐挳 active-low */
    /* [IO_IN_DOWN_BUTTON]   */ { GPIOE, GPIO_PIN_1, 0, 20 },  /* PE1 涓嬮檷鎸夐挳 active-low */
    /* [IO_IN_LOCK_BUTTON]   */ { GPIOE, GPIO_PIN_2, 0, 20 },  /* PE2 閿佸畾鎸夐挳 active-low */
    /* [IO_IN_ESTOP]         */ { GPIOE, GPIO_PIN_3, 1, 20 },  /* PE3 鎬ュ仠 active-high */
    /* [IO_IN_UPPER_LIMIT]   */ { GPIOE, GPIO_PIN_4, 1, 20 },  /* PE4 涓婇檺浣?active-high */
    /* [IO_IN_LOWER_LIMIT]   */ { GPIOE, GPIO_PIN_5, 1, 20 },  /* PE5 涓嬮檺浣嶏紙澶у壀鏈敤锛?*/
    /* [IO_IN_REFILL_BUTTON] */ { GPIOE, GPIO_PIN_6, 0, 20 },  /* PE6 琛ユ补鎸夐挳 active-low */
    /* [IO_IN_PHOTOELECTRIC] */ { GPIOE, GPIO_PIN_7, 1, 50 },  /* PE7 鍏夌數 active-high */
    /* [IO_IN_ROTARY_SWITCH] */ { GPIOE, GPIO_PIN_8, 1, 50 },  /* PE8 鏃嬭浆寮€鍏?active-high */
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
};

static io_out_pin_t s_out_map[IO_OUT_MAX] = {
    /* [IO_OUT_MOTOR]           */ { GPIOF, GPIO_PIN_8,  1 },  /* PF8 鐢垫満缁х數鍣?楂樼數骞冲惛鍚?*/
    /* [IO_OUT_DROP_VALVE]      */ { GPIOF, GPIO_PIN_9,  1 },  /* PF9 涓嬮檷闃€ 楂樼數骞冲惛鍚?*/
    /* [IO_OUT_MAIN_AIR_VALVE]  */ { GPIOD, GPIO_PIN_8,  1 },  /* PD8 涓绘満姘旈榾 楂樼數骞冲惛鍚?*/
    /* [IO_OUT_MAIN_WORK_VALVE] */ { GPIOD, GPIO_PIN_9,  1 },  /* PD9 涓绘満宸ヤ綔闃€ 楂樼數骞冲惛鍚?*/
    /* [IO_OUT_SUB_AIR_VALVE]   */ { GPIOD, GPIO_PIN_10, 1 },  /* PD10 瀛愭満姘旈榾 楂樼數骞冲惛鍚?*/
    /* [IO_OUT_SUB_WORK_VALVE]  */ { GPIOD, GPIO_PIN_11, 1 },  /* PD11 瀛愭満宸ヤ綔闃€ 楂樼數骞冲惛鍚?*/
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

static const char *io_pin_name(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOG && pin == GPIO_PIN_15) return "PG15";
    if (port == GPIOE && pin == GPIO_PIN_0)  return "PE0";
    if (port == GPIOE && pin == GPIO_PIN_1)  return "PE1";
    if (port == GPIOE && pin == GPIO_PIN_2)  return "PE2";
    if (port == GPIOE && pin == GPIO_PIN_3)  return "PE3";
    if (port == GPIOE && pin == GPIO_PIN_4)  return "PE4";
    if (port == GPIOE && pin == GPIO_PIN_5)  return "PE5";
    if (port == GPIOE && pin == GPIO_PIN_6)  return "PE6";
    if (port == GPIOE && pin == GPIO_PIN_7)  return "PE7";
    if (port == GPIOE && pin == GPIO_PIN_8)  return "PE8";
    return "NC";
}

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
            id == IO_IN_UPPER_LIMIT ||
            id == IO_IN_LOWER_LIMIT ||
            id == IO_IN_PHOTOELECTRIC) ? 1U : 0U;
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
    uint8_t physical = io_read_physical(id);
    uint8_t normalized = io_normalize(id, physical);
    const char *pin = io_pin_name(s_in_map[id].port, s_in_map[id].pin);

    if ((normalized != 0U) && (io_is_warning_input(id) != 0U)) {
        const char *tag = io_warning_tag(id);
        elog_w(tag, "[%s] triggered at boot source=%s id=%d pin=%s state=%s norm=%u phy=%u active=%u debounce=%u",
               tag,
               s_in_name[id],
               (int)id,
               pin,
               io_state_text(normalized),
               (unsigned int)normalized,
               (unsigned int)physical,
               (unsigned int)s_in_map[id].active_level,
               (unsigned int)s_in_map[id].debounce_ms);
    } else {
        elog_i("IOMAP", "[IOMAP] input init id=%d name=%s pin=%s state=%s norm=%u phy=%u active=%u debounce=%u",
               (int)id,
               s_in_name[id],
               pin,
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
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

/* ============ 瀵瑰鎺ュ彛瀹炵幇 ============ */

void App_IO_Map_Init(product_type_t type)
{
    /* 浣胯兘鎵€鏈夊彲鑳界敤鍒扮殑 GPIO 鏃堕挓 */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    if (type == PRODUCT_TYPE_DOUBLE_POST) {
        /* ===== 涓ゆ煴锛? 杈撳叆 3 杈撳嚭 ===== */
        /* 杈撳叆鏄犲皠锛圥E0-PE4锛?*/
        s_in_map[IO_IN_UP_BUTTON]     = (io_in_pin_t){ GPIOG, GPIO_PIN_15, 0, 20 };  /* PE0 涓婂崌鎸夐挳 active-low */
        s_in_map[IO_IN_DOWN_BUTTON]   = (io_in_pin_t){ GPIOE, GPIO_PIN_0, 0, 20 };  /* PE1 涓嬮檷鎸夐挳 active-low */
        s_in_map[IO_IN_LOCK_BUTTON]   = (io_in_pin_t){ GPIOE, GPIO_PIN_1, 0, 20 };  /* PE2 閿佸畾鎸夐挳 active-low */
        s_in_map[IO_IN_ESTOP]         = (io_in_pin_t){ GPIOE, GPIO_PIN_2, 0, 20 };  /* PE3 鎬ュ仠 NC active-high */
        s_in_map[IO_IN_UPPER_LIMIT]   = (io_in_pin_t){ GPIOE, GPIO_PIN_5, 1, 20 };  /* PE4 涓婇檺浣?NC active-high */
        s_in_map[IO_IN_LOWER_LIMIT]   = (io_in_pin_t){ NULL, 0, 0, 0 };             /* 鏈敤 */
        s_in_map[IO_IN_REFILL_BUTTON] = (io_in_pin_t){ NULL, 0, 0, 0 };             /* 鏈敤 */
        s_in_map[IO_IN_PHOTOELECTRIC] = (io_in_pin_t){ NULL, 0, 0, 0 };             /* 鏈敤 */
        s_in_map[IO_IN_ROTARY_SWITCH] = (io_in_pin_t){ NULL, 0, 0, 0 };             /* 鏈敤 */

        /* 杈撳嚭鏄犲皠锛圥D8/PD9/PD10锛夆€?涓ゆ煴缁х數鍣ㄩ珮鐢靛钩鍚稿悎 */
        s_out_map[IO_OUT_MOTOR]           = (io_out_pin_t){ GPIOF, GPIO_PIN_8,  1 };  /* PD8 鐢垫満缁х數鍣?楂樼數骞冲惛鍚?*/
        s_out_map[IO_OUT_MAIN_AIR_VALVE]  = (io_out_pin_t){ GPIOF, GPIO_PIN_9,  1 };  /* PF9 鐢电閾?楂樼數骞冲惛鍚堬紙澶嶇敤涓绘満姘旈榾妲戒綅锛?*/
        s_out_map[IO_OUT_DROP_VALVE]      = (io_out_pin_t){ GPIOD, GPIO_PIN_8,  1 };  /* PD8 涓嬮檷闃€ 楂樼數骞冲惛鍚?*/
        s_out_map[IO_OUT_MAIN_WORK_VALVE] = (io_out_pin_t){ NULL, 0, 0 };             /* 鏈敤 */
        s_out_map[IO_OUT_SUB_AIR_VALVE]   = (io_out_pin_t){ NULL, 0, 0 };             /* 鏈敤 */
        s_out_map[IO_OUT_SUB_WORK_VALVE]  = (io_out_pin_t){ NULL, 0, 0 };             /* 鏈敤 */

        /* PE0-PE2 宸茬敱 CubeMX 閰嶇疆涓轰笂鎷夎緭鍏ワ紙鍙屾煴 Up/Down/Stop_Key锛?         * PE3(鎬ュ仠)/PE4(涓婇檺浣? 闇€浠ｇ爜鍒濆鍖栦负涓婃媺杈撳叆 */
        io_init_input(GPIOG, GPIO_PIN_15, 1); /* PG15 涓婂崌鎸夐挳 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_0, 1);  /* PE0 涓嬮檷鎸夐挳 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_1, 1);  /* PE1 閿佸畾鎸夐挳 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_2, 1);  /* PE2 鎬ュ仠 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_5, 1);  /* PE4 涓婇檺浣?涓婃媺 */
        io_init_output(GPIOF, GPIO_PIN_8, 1); /* PF8 鐢垫満 */
        io_init_output(GPIOF, GPIO_PIN_9, 1); /* PF9 鐢电閾?*/
        io_init_output(GPIOD, GPIO_PIN_8, 1); /* PD8 涓嬮檷闃€ */

#if IO_MAP_DEBUG == 1
        elog_i("IOMAP", "[IOMAP] Double-post mapping: 5 in (PG15/PE0/PE1/PE2/PE5), 3 out (PF8/PF9/PD8)");
#endif
    } else {
        /* ===== 澶у壀锛? 杈撳叆 6 杈撳嚭锛堜繚鐣欏師鏈夐潤鎬佸垵濮嬪寲鏄犲皠锛?===== */
        __HAL_RCC_GPIOF_CLK_ENABLE();  /* PF8/PF9 澶у壀涓撶敤 */

        /* PE3/PE4/PE6/PE7/PE8 闇€瑕佸垵濮嬪寲锛堝弻鏌卞師鏈槸 PE5-PE8 闃叉挒鏉嗕笅鎷夎緭鍏ワ級 */
        io_init_input(GPIOE, GPIO_PIN_2, 1);  /* PE3 鎬ュ仠 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_5, 1);  /* PE4 涓婇檺浣?涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_3, 1);  /* PE6 琛ユ补鎸夐挳 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_8, 1);  /* PE7 鍏夌數 涓婃媺 */
        io_init_input(GPIOE, GPIO_PIN_8, 1);  /* PE8 鏃嬭浆寮€鍏?涓婃媺 */

        /* PF8/PF9 闇€瑕佹柊澧炲垵濮嬪寲锛圥D8-PD11 宸茬敱 CubeMX 閰嶇疆锛夛紝澶у壀楂樼數骞冲惛鍚?*/
        io_init_output(GPIOF, GPIO_PIN_8, 1);  /* PF8 鐢垫満 */
        io_init_output(GPIOF, GPIO_PIN_9, 1);  /* PF9 涓嬮檷闃€ */

#if IO_MAP_DEBUG == 1
        elog_i("IOMAP", "[IOMAP] Large-scissor mapping: 9 in (PE0-PE8), 6 out (PF8/PF9/PD8-PD11)");
#endif
    }

    /* 鍒濆鍖栧幓鎶栫姸鎬?*/
    for (int i = 0; i < IO_IN_MAX; i++) {
        if (s_in_map[i].port == NULL) {
            s_debounce[i].last_raw         = 0;
            s_debounce[i].stable           = 0;
            s_debounce[i].last_change_tick = HAL_GetTick();
            continue;
        }
        uint8_t physical = io_read_physical((io_in_id_t)i);
        uint8_t raw = io_normalize((io_in_id_t)i, physical);
        s_debounce[i].last_raw          = raw;
        s_debounce[i].stable            = raw;
        s_debounce[i].last_change_tick  = HAL_GetTick();
    }

    /* 鍒濆鍖栬緭鍑虹姸鎬佷负鍏ㄦ柇寮€锛堟寜鍚勫紩鑴?active_level 鍐欐柇寮€鐢靛钩锛?*/
    for (int i = 0; i < IO_OUT_MAX; i++) {
        s_out_state[i] = 0;
        if (s_out_map[i].port != NULL) {
            /* active_level=1锛堥珮鐢靛钩鍚稿悎锛夆啋 鏂紑=RESET锛堜綆鐢靛钩锛?             * active_level=0锛堜綆鐢靛钩鍚稿悎锛夆啋 鏂紑=SET锛堥珮鐢靛钩锛?             */
            GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
            HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
        }
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
    return io_normalize(id, io_read_physical(id));
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
        const char *pin = io_pin_name(s_in_map[id].port, s_in_map[id].pin);

        if ((normalized != 0U) && (io_is_warning_input(id) != 0U)) {
            const char *tag = io_warning_tag(id);
            elog_w(tag, "[%s] snapshot active reason=%s source=%s id=%d pin=%s phy=%u norm=%u stable=%u active=%u debounce=%u",
                   tag,
                   why,
                   s_in_name[i],
                   i,
                   pin,
                   (unsigned int)physical,
                   (unsigned int)normalized,
                   (unsigned int)s_debounce[i].stable,
                   (unsigned int)s_in_map[i].active_level,
                   (unsigned int)s_in_map[i].debounce_ms);
        } else {
            elog_i("IOMAP", "[IOMAP] in[%02d] %-10s pin=%s phy=%u norm=%u stable=%u active=%u debounce=%u",
                   i,
                   s_in_name[i],
                   pin,
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
        /* 鍘熷鍊煎凡绋冲畾瓒呰繃鍘绘姈鏃堕棿锛屾洿鏂扮ǔ瀹氬€?*/
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
                       io_pin_name(s_in_map[id].port, s_in_map[id].pin),
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
                       io_pin_name(s_in_map[id].port, s_in_map[id].pin),
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
                       io_pin_name(s_in_map[id].port, s_in_map[id].pin),
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

void App_IO_Write(io_out_id_t id, uint8_t value)
{
    if (id >= IO_OUT_MAX) return;
    if (s_out_map[id].port == NULL) return;  /* 鏈敤寮曡剼 */

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
        if (s_out_map[i].port == NULL) continue;  /* 鏈敤寮曡剼 */
        /* 鎸?active_level 鍐欐柇寮€鐢靛钩 */
        GPIO_PinState off_state = s_out_map[i].active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_out_map[i].port, s_out_map[i].pin, off_state);
    }
#if IO_MAP_DEBUG == 1
    elog_w("IOMAP", "[IOMAP] All outputs OFF");
#endif
}
