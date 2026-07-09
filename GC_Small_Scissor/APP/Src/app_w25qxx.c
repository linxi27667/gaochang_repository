/**
 * @file app_w25qxx.c
 * @brief W25Q APP wrapper for chip handshake plus in-memory config/stat/log stubs.
 */
#include "app_w25qxx.h"
#include "app_spi.h"
#include "app_product.h"
#include <string.h>

#ifndef MAX_PULSES
#define MAX_PULSES 0U
#endif

#if W25Q_DEBUG == 1
#include "elog.h"
#endif

/* ============ 全局对象 ============
 * W25Q 当前仅用作芯片握手（验证 SPI 通信 + 读 JEDEC ID），
 * 不进行任何配置/统计/日志的持久化。
 * - g_product_type 直接用编译时默认值（GC-LiangZhu=DOUBLE_POST, GC-DaJian=LARGE_SCISSOR）
 * - g_config 保留内存默认值，供代码运行时引用
 * - 所有 Load/Save 接口保留空实现以兼容头文件声明，避免链接错误
 * - 配置/统计数据后续走云端 DTU 持久化
 */

w25q_storage_t g_w25q_storage = {0};

w25q_config_t g_config = {
    .header                   = W25Q_CONFIG_HEADER,
    .version                  = W25Q_CONFIG_VERSION,
    .product_type             = PRODUCT_TYPE_SMALL_SCISSOR,
    .reserved1                = 0,
    .motor_to_valve_delay_ms  = 200,
    .motor_hold_ms            = 3000,
    .sub_motor_hold_ms        = 1500,
    .module_enable_mask       = 0,
    .reserved2                = 0,
    .photoelectric_debounce_ms= 50,
    .estop_debounce_ms        = 20,
    /* 旧双柱字段（保留兼容） */
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
    .bus = &SPI_Bus
};

/* ============ 系统初始化 ============
 * 只做 SPI 初始化 + W25Q 芯片握手 + 读 JEDEC ID 验证通信。
 * 不加载任何配置/统计/日志。
 * g_product_type 由 app_product.c 编译时确定，不依赖 Flash。
 */
void App_W25Qxx_System_Init(void)
{
    App_SPI_System_Init();

    uint8_t ret = W25Q_Init_Device(&W25Q_Flash);

    if (ret == W25Q_OK)
    {
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[W25Q] JEDEC=0x%06lX DEV=0x%04X (chip handshake OK, no flash storage)",
               W25Q_Read_JEDEC_ID(&W25Q_Flash),
               W25Q_Read_Device_ID(&W25Q_Flash));
        elog_i("W25Q", "[W25Q] product_type=%d (compile-time default, not from flash)",
               g_product_type);
        #endif
    }
    else
    {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] chip init FAILED (SPI comm error)");
        #endif
    }
}

uint32_t App_W25Qxx_Get_JEDEC_ID(void)
{
    return W25Q_Read_JEDEC_ID(&W25Q_Flash);
}

/* ============ Storage 接口（空实现） ============ */
void App_W25Qxx_Storage_Load(void)
{
    /* W25Q 不持久化，空操作 */
}

uint8_t App_W25Qxx_Storage_Save(void)
{
    return W25Q_OK;
}

/* ============ Height 接口（空实现，兼容旧声明） ============ */
void App_W25Qxx_Height_Load(void)
{
}

uint8_t App_W25Qxx_Height_Save(void)
{
    return W25Q_OK;
}

void App_W25Qxx_Height_Save_If_Needed(void)
{
}

uint8_t App_W25Qxx_Height_Is_Loaded(void)
{
    return 0;
}

/* ============ Config 接口（空实现） ============
 * g_config 使用 .c 文件里的默认初始化值，不从 Flash 加载。
 * 后续配置参数由云端 DTU 下发。
 */
void App_W25Qxx_Config_Load(void)
{
    /* 空操作：使用 g_config 编译时默认值 */
}

uint8_t App_W25Qxx_Config_Save(void)
{
    return W25Q_OK;
}

/* ============ Stats 接口 ============
 * g_stats 仅在内存中维护（供 IoT 上报），不写 Flash。
 */
void App_W25Qxx_Stats_Load(void)
{
    /* 空操作：使用 g_stats 默认值（全 0） */
}

uint8_t App_W25Qxx_Stats_Save(void)
{
    return W25Q_OK;
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    g_stats.up_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.up_count_main++;
    else                        g_stats.up_count_sub++;
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    g_stats.down_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.down_count_main++;
    else                        g_stats.down_count_sub++;
}

void App_W25Qxx_Stats_Inc_Lock(void)       { g_stats.lock_count++; }
void App_W25Qxx_Stats_Inc_Refill(void)     { g_stats.refill_count++; }
void App_W25Qxx_Stats_Inc_Estop(void)      { g_stats.estop_count++; }
void App_W25Qxx_Stats_Inc_PhotoAlarm(void) { g_stats.photo_alarm_count++; }
void App_W25Qxx_Stats_Add_RunMs(uint32_t ms) { g_stats.total_run_ms += ms; }

/* ============ OpLog 接口（空实现） ============ */
uint8_t App_W25Qxx_OpLog_Append(const w25q_op_log_entry_t *entry)
{
    (void)entry;
    return W25Q_OK;
}

uint8_t App_W25Qxx_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    (void)index;
    if (out) memset(out, 0, sizeof(*out));
    return W25Q_ERR;
}

uint16_t App_W25Qxx_OpLog_Count(void)
{
    return 0;
}

void App_W25Qxx_OpLog_Clear(void)
{
}
