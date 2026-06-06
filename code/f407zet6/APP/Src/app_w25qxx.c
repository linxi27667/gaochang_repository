#include "app_w25qxx.h"
#include "app_spi.h"
#include "motor.h"
#include "key.h"

#if W25Q_DEBUG == 1
#include "elog.h"
#endif

w25q_storage_t g_w25q_storage = {0};

w25q_config_t g_config = {
    .header                   = 0xA5A5,
    .tolerance_up             = 4,
    .tolerance_down           = 4,
    .stall_timeout_ms         = 2000,
    .balance_wait_max_ms      = 10000,
    .collision_debounce_ms    = 50,
    .secondary_descent_pulses = 30,
    .dual_column_mode         = 1,
    .screw_lead_mm            = 6,
    .max_pulses               = MAX_PULSES,
};

w25q_t W25Q_Flash = {
    .bus = &SPI_Bus
};

static uint8_t  g_height_loaded = 0;
static uint32_t g_height_sequence = 0;

static uint32_t storage_crc(const w25q_storage_t *s)
{
    return s->magic ^ s->debug_counter;
}

static uint8_t storage_validate(const w25q_storage_t *s)
{
    return (s->magic == W25Q_STORAGE_MAGIC) && (s->crc == storage_crc(s));
}

static void storage_set_crc(w25q_storage_t *s)
{
    s->crc = storage_crc(s);
}

static uint8_t storage_read_slot(w25q_storage_t *out, uint32_t addr)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, W25Q_STORAGE_SIZE) != W25Q_OK)
        return W25Q_ERR;
    return storage_validate(out) ? W25Q_OK : W25Q_ERR;
}

void App_W25Qxx_System_Init(void)
{
    App_SPI_System_Init();

    uint8_t ret = W25Q_Init_Device(&W25Q_Flash);

    if (ret == W25Q_OK)
    {
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] JEDEC=0x%06lX DEV=0x%04X",
               W25Q_Read_JEDEC_ID(&W25Q_Flash),
               W25Q_Read_Device_ID(&W25Q_Flash));
        #endif
        App_W25Qxx_Storage_Load();
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] ready");
        #endif
    }
    else
    {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] FAILED");
        #endif
    }
}

uint32_t App_W25Qxx_Get_JEDEC_ID(void)
{
    return W25Q_Read_JEDEC_ID(&W25Q_Flash);
}

void App_W25Qxx_Storage_Load(void)
{
    w25q_storage_t slot_a, slot_b;
    uint8_t a_ok = storage_read_slot(&slot_a, W25Q_SLOT_A_ADDR);
    uint8_t b_ok = storage_read_slot(&slot_b, W25Q_SLOT_B_ADDR);

    if (a_ok == W25Q_OK && b_ok == W25Q_OK)
    {
        g_w25q_storage = (slot_b.debug_counter > slot_a.debug_counter) ? slot_b : slot_a;
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] Storage loaded: counter=%lu (both valid, A=%lu B=%lu)",
               g_w25q_storage.debug_counter, slot_a.debug_counter, slot_b.debug_counter);
        #endif
    }
    else if (a_ok == W25Q_OK)
    {
        g_w25q_storage = slot_a;
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] Storage loaded: counter=%lu (slot A only)", g_w25q_storage.debug_counter);
        #endif
    }
    else if (b_ok == W25Q_OK)
    {
        g_w25q_storage = slot_b;
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] Storage loaded: counter=%lu (slot B only)", g_w25q_storage.debug_counter);
        #endif
    }
    else
    {
        #if W25Q_DEBUG == 1
        elog_w("W25Q", "[W25Q] No valid storage data, using defaults");
        #endif
        g_w25q_storage.magic = W25Q_STORAGE_MAGIC;
        g_w25q_storage.debug_counter = 0;
        g_w25q_storage.crc = storage_crc(&g_w25q_storage);
    }
}

uint8_t App_W25Qxx_Storage_Save(void)
{
    storage_set_crc(&g_w25q_storage);

    w25q_storage_t slot_a, slot_b;
    uint8_t a_ok = storage_read_slot(&slot_a, W25Q_SLOT_A_ADDR);
    uint8_t b_ok = storage_read_slot(&slot_b, W25Q_SLOT_B_ADDR);

    uint32_t target_addr;
    if (a_ok != W25Q_OK && b_ok != W25Q_OK)
        target_addr = W25Q_SLOT_A_ADDR;
    else if (a_ok != W25Q_OK)
        target_addr = W25Q_SLOT_A_ADDR;
    else if (b_ok != W25Q_OK)
        target_addr = W25Q_SLOT_B_ADDR;
    else if (slot_a.debug_counter <= slot_b.debug_counter)
        target_addr = W25Q_SLOT_A_ADDR;
    else
        target_addr = W25Q_SLOT_B_ADDR;

    #if W25Q_DEBUG == 1
    elog_i("W25Q", "[W25Q] Save slot=0x%08lX counter=%lu", target_addr, g_w25q_storage.debug_counter);
    #endif

    uint8_t ret = W25Q_Sector_Erase(&W25Q_Flash, target_addr);
    if (ret != W25Q_OK) {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] Sector erase FAILED");
        #endif
        return W25Q_ERR;
    }

    ret = W25Q_Page_Program(&W25Q_Flash, target_addr, (uint8_t *)&g_w25q_storage, W25Q_STORAGE_SIZE);
    #if W25Q_DEBUG == 1
    if (ret != W25Q_OK)
        elog_e("W25Q", "[W25Q] Page program FAILED");
    else
        elog_i("W25Q", "[W25Q] Page program OK");
    #endif

    return ret;
}

static uint32_t Height_CRC32(const w25q_height_t *storage)
{
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *data = (const uint8_t *)storage;
    for (uint16_t i = 0; i < sizeof(w25q_height_t) - 4; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return crc;
}

static const char *height_slot_name(uint32_t addr)
{
    return (addr == HEIGHT_SLOT_A_ADDR) ? "A" : "B";
}

static uint8_t height_validate(const w25q_height_t *storage, const char **reason)
{
    if (storage->magic != W25Q_HEIGHT_MAGIC) {
        if (reason) *reason = "bad_magic";
        return W25Q_ERR;
    }
    if (storage->version != W25Q_HEIGHT_VERSION) {
        if (reason) *reason = "bad_version";
        return W25Q_ERR;
    }
    if (storage->crc != Height_CRC32(storage)) {
        if (reason) *reason = "bad_crc";
        return W25Q_ERR;
    }
    if (reason) *reason = "ok";
    return W25Q_OK;
}

static uint8_t height_read_slot(uint32_t addr, w25q_height_t *out, const char **reason)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, sizeof(*out)) != W25Q_OK) {
        if (reason) *reason = "read_failed";
        return W25Q_ERR;
    }

    return height_validate(out, reason);
}

void App_W25Qxx_Height_Load(void)
{
    w25q_height_t slot_a = {0};
    w25q_height_t slot_b = {0};
    const char *reason_a = "unknown";
    const char *reason_b = "unknown";
    uint8_t a_ok = height_read_slot(HEIGHT_SLOT_A_ADDR, &slot_a, &reason_a);
    uint8_t b_ok = height_read_slot(HEIGHT_SLOT_B_ADDR, &slot_b, &reason_b);

    #if W25Q_DEBUG == 1
    elog_i("W25Q", "[W25Q] Height slot A: %s seq=%lu left=%ld right=%ld crc=0x%08lX",
           reason_a, slot_a.sequence, slot_a.heights[0], slot_a.heights[1], slot_a.crc);
    elog_i("W25Q", "[W25Q] Height slot B: %s seq=%lu left=%ld right=%ld crc=0x%08lX",
           reason_b, slot_b.sequence, slot_b.heights[0], slot_b.heights[1], slot_b.crc);
    #endif

    if (a_ok == W25Q_OK && (b_ok != W25Q_OK || slot_a.sequence >= slot_b.sequence)) {
        g_column[0].pulse_count = slot_a.heights[0];
        g_column[1].pulse_count = slot_a.heights[1];
        g_height_sequence = slot_a.sequence;
        g_height_loaded = 1;
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] Flash: loaded slot=A seq=%lu left=%ldmm right=%ldmm",
               g_height_sequence, HEIGHT_MM(slot_a.heights[0]), HEIGHT_MM(slot_a.heights[1]));
        #endif
        return;
    }

    if (b_ok == W25Q_OK) {
        g_column[0].pulse_count = slot_b.heights[0];
        g_column[1].pulse_count = slot_b.heights[1];
        g_height_sequence = slot_b.sequence;
        g_height_loaded = 1;
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] Flash: loaded slot=B seq=%lu left=%ldmm right=%ldmm",
               g_height_sequence, HEIGHT_MM(slot_b.heights[0]), HEIGHT_MM(slot_b.heights[1]));
        #endif
        return;
    }

    g_height_loaded = 0;
    g_height_sequence = 0;
    #if W25Q_DEBUG == 1
    elog_w("W25Q", "[W25Q] Flash: no valid height, defaults left=0mm right=0mm");
    #endif
}

uint8_t App_W25Qxx_Height_Save(void)
{
    w25q_height_t data = {0};
    w25q_height_t slot_a = {0};
    w25q_height_t slot_b = {0};
    w25q_height_t verify = {0};
    const char *reason_a = "unknown";
    const char *reason_b = "unknown";
    uint8_t a_ok = height_read_slot(HEIGHT_SLOT_A_ADDR, &slot_a, &reason_a);
    uint8_t b_ok = height_read_slot(HEIGHT_SLOT_B_ADDR, &slot_b, &reason_b);
    uint32_t target_addr;

    (void)reason_a;
    (void)reason_b;

    if (a_ok != W25Q_OK && b_ok != W25Q_OK)
        target_addr = HEIGHT_SLOT_A_ADDR;
    else if (a_ok != W25Q_OK)
        target_addr = HEIGHT_SLOT_A_ADDR;
    else if (b_ok != W25Q_OK)
        target_addr = HEIGHT_SLOT_B_ADDR;
    else if (slot_a.sequence <= slot_b.sequence)
        target_addr = HEIGHT_SLOT_A_ADDR;
    else
        target_addr = HEIGHT_SLOT_B_ADDR;

    data.magic = W25Q_HEIGHT_MAGIC;
    data.version = W25Q_HEIGHT_VERSION;
    data.sequence = g_height_sequence + 1;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    data.heights[0] = g_column[0].pulse_count;
    data.heights[1] = g_column[1].pulse_count;
    __set_PRIMASK(primask);

    data.crc = Height_CRC32(&data);

    #if W25Q_DEBUG == 1
    elog_i("W25Q", "[W25Q] Height save begin slot=%s seq=%lu left=%ldmm right=%ldmm crc=0x%08lX",
           height_slot_name(target_addr), data.sequence,
           HEIGHT_MM(data.heights[0]), HEIGHT_MM(data.heights[1]), data.crc);
    #endif

    uint8_t result = W25Q_Sector_Erase(&W25Q_Flash, target_addr);
    if (result != W25Q_OK) {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] Height save erase FAILED slot=%s", height_slot_name(target_addr));
        #endif
        return result;
    }

    result = W25Q_Page_Program(&W25Q_Flash, target_addr, (uint8_t *)&data, sizeof(data));
    if (result != W25Q_OK) {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] Height save program FAILED slot=%s", height_slot_name(target_addr));
        #endif
        return result;
    }

    if (W25Q_Read_Buffer(&W25Q_Flash, target_addr, (uint8_t *)&verify, sizeof(verify)) != W25Q_OK
        || height_validate(&verify, NULL) != W25Q_OK
        || verify.sequence != data.sequence
        || verify.heights[0] != data.heights[0]
        || verify.heights[1] != data.heights[1]) {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] Height save verify FAILED slot=%s", height_slot_name(target_addr));
        #endif
        return W25Q_ERR;
    }

    g_height_sequence = data.sequence;
    g_height_loaded = 1;

    #if W25Q_DEBUG == 1
    elog_i("W25Q", "[W25Q] Height save OK slot=%s seq=%lu left=%ld right=%ld",
           height_slot_name(target_addr), data.sequence, data.heights[0], data.heights[1]);
    #endif

    return W25Q_OK;
}

void App_W25Qxx_Height_Save_If_Needed(void)
{
    static uint8_t pending_save = 0;
    static uint32_t stop_tick = 0;

    if (g_command.direction != DIR_STOP) {
        pending_save = 1;
        stop_tick = 0;
        return;
    }

    if (!pending_save) {
        return;
    }

    if (stop_tick == 0) {
        stop_tick = HAL_GetTick();
        return;
    }

    if (HAL_GetTick() - stop_tick < 200) return;

    if (App_W25Qxx_Height_Save() == W25Q_OK) {
        pending_save = 0;
    }
    stop_tick = 0;
}

uint8_t App_W25Qxx_Height_Is_Loaded(void)
{
    return g_height_loaded;
}
