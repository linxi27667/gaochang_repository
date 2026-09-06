#include "app_w25qxx.h"
#include "app_product.h"
#include <stddef.h>
#include <string.h>

#if W25Q_DEBUG == 1
#include "elog.h"
#endif

w25q_storage_t g_w25q_storage = {0};

w25q_config_t g_config = {
    .header                   = W25Q_CONFIG_HEADER,
    .version                  = W25Q_CONFIG_VERSION,
    .product_type             = PRODUCT_TYPE_DOUBLE_POST,
    .reserved1                = 0,
    .motor_to_valve_delay_ms  = 200,
    .motor_hold_ms            = 3000,
    .sub_motor_hold_ms        = 1500,
    .module_enable_mask       = 0,
    .reserved2                = 0,
    .photoelectric_debounce_ms= 50,
    .estop_debounce_ms        = 20,
    .tolerance_up             = 4,
    .tolerance_down           = 4,
    .stall_timeout_ms         = 2000,
    .balance_wait_max_ms      = 10000,
    .collision_debounce_ms    = 50,
    .secondary_descent_pulses = 30,
    .dual_column_mode         = 1,
    .screw_lead_mm            = 8,
    .max_pulses               = MAX_PULSES,
};

w25q_stats_t g_stats = {0};

w25q_t W25Q_Flash = {
    .unused = 0
};

/* OpLog：16B×16 环形。满则覆盖最旧槽（FRAM 可直接覆写）；Clear 时整扇区擦除。 */
static uint16_t s_oplog_count;
static uint16_t s_oplog_head;
static uint8_t  s_oplog_ready;

/* CRC16-CCITT（与 rise_counter journal 同款：poly 0x1021, init 0xFFFF） */
static uint16_t App_W25Qxx_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint32_t App_W25Qxx_Crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t bit;

    while (len-- != 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
    }
    return ~crc;
}

/* ---------- Storage ---------- */

static uint32_t storage_crc(const w25q_storage_t *s)
{
    return s->magic ^ s->debug_counter;
}

static uint8_t storage_validate(const w25q_storage_t *s)
{
    return (s->magic == W25Q_STORAGE_MAGIC) && (s->crc == storage_crc(s));
}

static uint8_t storage_read_slot(w25q_storage_t *out, uint32_t addr)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, W25Q_STORAGE_SIZE) != W25Q_OK)
        return W25Q_ERR;
    return storage_validate(out) ? W25Q_OK : W25Q_ERR;
}

void App_W25Qxx_Storage_Load(void)
{
    w25q_storage_t slot_a, slot_b;
    uint8_t a_ok = storage_read_slot(&slot_a, W25Q_SLOT_A_ADDR);
    uint8_t b_ok = storage_read_slot(&slot_b, W25Q_SLOT_B_ADDR);

    if (a_ok == W25Q_OK && b_ok == W25Q_OK)
        g_w25q_storage = (slot_b.debug_counter > slot_a.debug_counter) ? slot_b : slot_a;
    else if (a_ok == W25Q_OK)
        g_w25q_storage = slot_a;
    else if (b_ok == W25Q_OK)
        g_w25q_storage = slot_b;
    else
    {
        g_w25q_storage.magic = W25Q_STORAGE_MAGIC;
        g_w25q_storage.debug_counter = 0U;
        g_w25q_storage.crc = storage_crc(&g_w25q_storage);
    }
}

uint8_t App_W25Qxx_Storage_Save(void)
{
    w25q_storage_t slot_a, slot_b;
    uint8_t a_ok, b_ok;
    uint32_t target_addr;

    g_w25q_storage.magic = W25Q_STORAGE_MAGIC;
    g_w25q_storage.crc = storage_crc(&g_w25q_storage);

    a_ok = storage_read_slot(&slot_a, W25Q_SLOT_A_ADDR);
    b_ok = storage_read_slot(&slot_b, W25Q_SLOT_B_ADDR);

    if (a_ok != W25Q_OK)
        target_addr = W25Q_SLOT_A_ADDR;
    else if (b_ok != W25Q_OK)
        target_addr = W25Q_SLOT_B_ADDR;
    else if (slot_a.debug_counter <= slot_b.debug_counter)
        target_addr = W25Q_SLOT_A_ADDR;
    else
        target_addr = W25Q_SLOT_B_ADDR;

    if (W25Q_Sector_Erase(&W25Q_Flash, target_addr) != W25Q_OK)
        return W25Q_ERR;
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (uint8_t *)&g_w25q_storage, W25Q_STORAGE_SIZE);
}

/* ---------- Config（sequence 存于 reserved[0..3]） ---------- */

static uint32_t config_get_seq(const w25q_config_t *cfg)
{
    uint32_t seq = 0U;
    memcpy(&seq, cfg->reserved, sizeof(seq));
    return seq;
}

static void config_set_seq(w25q_config_t *cfg, uint32_t seq)
{
    memcpy(cfg->reserved, &seq, sizeof(seq));
}

static uint8_t config_validate(const w25q_config_t *cfg)
{
    uint16_t crc;

    if (cfg->header != W25Q_CONFIG_HEADER)
        return W25Q_ERR;
    crc = App_W25Qxx_Crc16((const uint8_t *)cfg,
                           (uint16_t)offsetof(w25q_config_t, crc16));
    return (crc == cfg->crc16) ? W25Q_OK : W25Q_ERR;
}

static uint8_t config_read_slot(w25q_config_t *out, uint32_t addr)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, sizeof(*out)) != W25Q_OK)
        return W25Q_ERR;
    return config_validate(out);
}

void App_W25Qxx_Config_Load(void)
{
    w25q_config_t slot_a, slot_b;
    uint8_t a_ok = config_read_slot(&slot_a, W25Q_CONFIG_SLOT_A_ADDR);
    uint8_t b_ok = config_read_slot(&slot_b, W25Q_CONFIG_SLOT_B_ADDR);

    if (a_ok == W25Q_OK && b_ok == W25Q_OK)
        g_config = (config_get_seq(&slot_b) > config_get_seq(&slot_a)) ? slot_b : slot_a;
    else if (a_ok == W25Q_OK)
        g_config = slot_a;
    else if (b_ok == W25Q_OK)
        g_config = slot_b;
    /* 失败：保留编译期默认 g_config */
}

uint8_t App_W25Qxx_Config_Save(void)
{
    w25q_config_t slot_a, slot_b;
    uint8_t a_ok, b_ok;
    uint32_t target_addr;
    uint32_t seq;

    a_ok = config_read_slot(&slot_a, W25Q_CONFIG_SLOT_A_ADDR);
    b_ok = config_read_slot(&slot_b, W25Q_CONFIG_SLOT_B_ADDR);

    if (a_ok != W25Q_OK)
        target_addr = W25Q_CONFIG_SLOT_A_ADDR;
    else if (b_ok != W25Q_OK)
        target_addr = W25Q_CONFIG_SLOT_B_ADDR;
    else if (config_get_seq(&slot_a) <= config_get_seq(&slot_b))
        target_addr = W25Q_CONFIG_SLOT_A_ADDR;
    else
        target_addr = W25Q_CONFIG_SLOT_B_ADDR;

    seq = config_get_seq(&g_config) + 1U;
    if (a_ok == W25Q_OK || b_ok == W25Q_OK)
    {
        uint32_t newest = 0U;
        if (a_ok == W25Q_OK) newest = config_get_seq(&slot_a);
        if (b_ok == W25Q_OK && config_get_seq(&slot_b) > newest)
            newest = config_get_seq(&slot_b);
        seq = newest + 1U;
    }
    config_set_seq(&g_config, seq);

    g_config.header = W25Q_CONFIG_HEADER;
    g_config.crc16 = App_W25Qxx_Crc16((const uint8_t *)&g_config,
                                      (uint16_t)offsetof(w25q_config_t, crc16));

    if (W25Q_Sector_Erase(&W25Q_Flash, target_addr) != W25Q_OK)
        return W25Q_ERR;
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (uint8_t *)&g_config, sizeof(g_config));
}

/* ---------- Stats（reserved 作 save sequence） ---------- */

static uint32_t stats_crc(const w25q_stats_t *s)
{
    return App_W25Qxx_Crc32((const uint8_t *)s, (uint32_t)offsetof(w25q_stats_t, crc));
}

static uint8_t stats_validate(const w25q_stats_t *s)
{
    if (s->magic != W25Q_STATS_MAGIC || s->version != W25Q_STATS_VERSION)
        return W25Q_ERR;
    return (s->crc == stats_crc(s)) ? W25Q_OK : W25Q_ERR;
}

static uint8_t stats_read_slot(w25q_stats_t *out, uint32_t addr)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, sizeof(*out)) != W25Q_OK)
        return W25Q_ERR;
    return stats_validate(out);
}

void App_W25Qxx_Stats_Load(void)
{
    w25q_stats_t slot_a, slot_b;
    uint8_t a_ok = stats_read_slot(&slot_a, W25Q_STATS_SLOT_A_ADDR);
    uint8_t b_ok = stats_read_slot(&slot_b, W25Q_STATS_SLOT_B_ADDR);

    if (a_ok == W25Q_OK && b_ok == W25Q_OK)
        g_stats = (slot_b.reserved >= slot_a.reserved) ? slot_b : slot_a;
    else if (a_ok == W25Q_OK)
        g_stats = slot_a;
    else if (b_ok == W25Q_OK)
        g_stats = slot_b;
    else
    {
        memset(&g_stats, 0, sizeof(g_stats));
        g_stats.magic = W25Q_STATS_MAGIC;
        g_stats.version = W25Q_STATS_VERSION;
    }
}

uint8_t App_W25Qxx_Stats_Save(void)
{
    w25q_stats_t slot_a, slot_b;
    uint8_t a_ok, b_ok;
    uint32_t target_addr;
    uint16_t seq;

    a_ok = stats_read_slot(&slot_a, W25Q_STATS_SLOT_A_ADDR);
    b_ok = stats_read_slot(&slot_b, W25Q_STATS_SLOT_B_ADDR);

    if (a_ok != W25Q_OK)
        target_addr = W25Q_STATS_SLOT_A_ADDR;
    else if (b_ok != W25Q_OK)
        target_addr = W25Q_STATS_SLOT_B_ADDR;
    else if (slot_a.reserved <= slot_b.reserved)
        target_addr = W25Q_STATS_SLOT_A_ADDR;
    else
        target_addr = W25Q_STATS_SLOT_B_ADDR;

    seq = g_stats.reserved + 1U;
    if (a_ok == W25Q_OK || b_ok == W25Q_OK)
    {
        uint16_t newest = 0U;
        if (a_ok == W25Q_OK) newest = slot_a.reserved;
        if (b_ok == W25Q_OK && slot_b.reserved > newest)
            newest = slot_b.reserved;
        seq = (uint16_t)(newest + 1U);
    }

    g_stats.magic = W25Q_STATS_MAGIC;
    g_stats.version = W25Q_STATS_VERSION;
    g_stats.reserved = seq;
    g_stats.crc = stats_crc(&g_stats);

    if (W25Q_Sector_Erase(&W25Q_Flash, target_addr) != W25Q_OK)
        return W25Q_ERR;
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (uint8_t *)&g_stats, sizeof(g_stats));
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    g_stats.up_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.up_count_main++;
    else                        g_stats.up_count_sub++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    g_stats.down_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.down_count_main++;
    else                        g_stats.down_count_sub++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Lock(void)
{
    g_stats.lock_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Refill(void)
{
    g_stats.refill_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Estop(void)
{
    g_stats.estop_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_PhotoAlarm(void)
{
    g_stats.photo_alarm_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Add_RunMs(uint32_t ms)
{
    g_stats.total_run_ms += ms;
    (void)App_W25Qxx_Stats_Save();
}

/* ---------- OpLog ---------- */

static uint8_t oplog_entry_erased(const w25q_op_log_entry_t *entry)
{
    const uint8_t *bytes = (const uint8_t *)entry;
    uint16_t i;

    for (i = 0U; i < sizeof(*entry); i++)
    {
        if (bytes[i] != 0xFFU)
            return 0U;
    }
    return 1U;
}

static void oplog_scan(void)
{
    w25q_op_log_entry_t entry;
    uint16_t i;
    uint16_t count = 0U;
    uint16_t newest_idx = 0U;
    uint32_t newest_ts = 0U;
    uint8_t found = 0U;

    for (i = 0U; i < W25Q_OPLOG_MAX_ENTRIES; i++)
    {
        uint32_t addr = W25Q_OPLOG_ADDR + ((uint32_t)i * W25Q_OPLOG_ENTRY_SIZE);
        if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)&entry, sizeof(entry)) != W25Q_OK)
            continue;
        if (oplog_entry_erased(&entry) != 0U)
            continue;
        count++;
        if (found == 0U || entry.timestamp >= newest_ts)
        {
            newest_ts = entry.timestamp;
            newest_idx = i;
            found = 1U;
        }
    }

    s_oplog_count = count;
    if (count == 0U)
        s_oplog_head = 0U;
    else if (count >= W25Q_OPLOG_MAX_ENTRIES)
        s_oplog_head = (uint16_t)((newest_idx + 1U) % W25Q_OPLOG_MAX_ENTRIES);
    else
        s_oplog_head = (uint16_t)((newest_idx + 1U) % W25Q_OPLOG_MAX_ENTRIES);
    s_oplog_ready = 1U;
}

uint8_t App_W25Qxx_OpLog_Append(const w25q_op_log_entry_t *entry)
{
    uint32_t addr;

    if (entry == NULL)
        return W25Q_ERR;
    if (s_oplog_ready == 0U)
        oplog_scan();

    addr = W25Q_OPLOG_ADDR + ((uint32_t)s_oplog_head * W25Q_OPLOG_ENTRY_SIZE);
    if (W25Q_Page_Program(&W25Q_Flash, addr, (const uint8_t *)entry, sizeof(*entry)) != W25Q_OK)
        return W25Q_ERR;

    s_oplog_head = (uint16_t)((s_oplog_head + 1U) % W25Q_OPLOG_MAX_ENTRIES);
    if (s_oplog_count < W25Q_OPLOG_MAX_ENTRIES)
        s_oplog_count++;
    return W25Q_OK;
}

uint8_t App_W25Qxx_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    uint16_t oldest;
    uint32_t addr;

    if (out == NULL)
        return W25Q_ERR;
    if (s_oplog_ready == 0U)
        oplog_scan();
    if (index >= s_oplog_count)
        return W25Q_ERR;

    /* index=0 为最旧 */
    if (s_oplog_count >= W25Q_OPLOG_MAX_ENTRIES)
        oldest = s_oplog_head;
    else
        oldest = 0U;

    addr = W25Q_OPLOG_ADDR +
           ((uint32_t)((oldest + index) % W25Q_OPLOG_MAX_ENTRIES) * W25Q_OPLOG_ENTRY_SIZE);
    return W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, sizeof(*out));
}

uint16_t App_W25Qxx_OpLog_Count(void)
{
    if (s_oplog_ready == 0U)
        oplog_scan();
    return s_oplog_count;
}

void App_W25Qxx_OpLog_Clear(void)
{
    (void)W25Q_Sector_Erase(&W25Q_Flash, W25Q_OPLOG_ADDR);
    s_oplog_count = 0U;
    s_oplog_head = 0U;
    s_oplog_ready = 1U;
}

/* ---------- Height stubs（双柱 FRAM 方案暂不落盘高度） ---------- */

void App_W25Qxx_Height_Load(void) {}
uint8_t App_W25Qxx_Height_Save(void) { return W25Q_OK; }
void App_W25Qxx_Height_Save_If_Needed(void) {}
uint8_t App_W25Qxx_Height_Is_Loaded(void) { return 0U; }

/* ---------- System ---------- */

void App_W25Qxx_System_Init(void)
{
    uint8_t ret = W25Q_Init_Device(&W25Q_Flash);

    if (ret == W25Q_OK)
    {
        App_W25Qxx_Storage_Load();
        App_W25Qxx_Config_Load();
        App_W25Qxx_Stats_Load();
        /* Rise journal / maintenance ledger：由 main 中 App_RiseCounter_Init 完成 */
#if W25Q_DEBUG == 1
        elog_i("W25Q", "[FRAM] JEDEC=0x%06lX ready product=%d",
               W25Q_Read_JEDEC_ID(&W25Q_Flash), g_product_type);
#endif
    }
    else
    {
#if W25Q_DEBUG == 1
        elog_e("W25Q", "[FRAM] init FAILED");
#endif
    }
}

uint32_t App_W25Qxx_Get_JEDEC_ID(void)
{
    return W25Q_Read_JEDEC_ID(&W25Q_Flash);
}
