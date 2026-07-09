/**
 * @file app_w25qxx.h
 * @brief APP-level W25Q flash data model, defaults, stats, and log interfaces.
 */
#ifndef __APP_W25QXX_H__
#define __APP_W25QXX_H__

#include <stdint.h>
#include "bsp_w25qxx.h"
#include "main.h"
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ W25Q 存储地址规划 ============
 * 0x000000  系统 storage (双扇区轮转)
 * 0x001000  系统 storage 备份
 * 0x002000  config (双扇区轮转) / double_post height slot A
 * 0x003000  config 备份 / double_post height slot B
 * 0x004000  运行统计 stats (双扇区轮转)
 * 0x005000  运行统计 stats 备份
 * 0x006000  操作日志环形缓冲 (4KB, 256 条)
 * 0x007000  报警日志环形缓冲
 * 0x008000+ 预留 (OTA 等)
 */

#define W25Q_SLOT_A_ADDR        0x00000000U
#define W25Q_SLOT_B_ADDR        0x00001000U

/* 配置 / 高度共用地址（双柱用 height，大剪用 config，互斥） */
#define W25Q_CONFIG_SLOT_A_ADDR 0x00002000U
#define W25Q_CONFIG_SLOT_B_ADDR 0x00003000U
#define HEIGHT_SLOT_A_ADDR      W25Q_CONFIG_SLOT_A_ADDR
#define HEIGHT_SLOT_B_ADDR      W25Q_CONFIG_SLOT_B_ADDR
#define HEIGHT_FLASH_ADDR       HEIGHT_SLOT_A_ADDR

/* 运行统计 */
#define W25Q_STATS_SLOT_A_ADDR  0x00004000U
#define W25Q_STATS_SLOT_B_ADDR  0x00005000U

/* 操作日志环形缓冲 */
#define W25Q_OPLOG_ADDR         0x00006000U
#define W25Q_OPLOG_SECTOR_SIZE  0x00001000U   /* 4KB */
#define W25Q_OPLOG_MAX_ENTRIES  (W25Q_OPLOG_SECTOR_SIZE / 16U)  /* 256 条 */

/* ============ 持久化数据结构 ============ */

typedef struct
{
    uint32_t magic;
    uint32_t debug_counter;
    uint32_t crc;
} w25q_storage_t;

#define W25Q_STORAGE_MAGIC      0x53544F52U
#define W25Q_STORAGE_SIZE       sizeof(w25q_storage_t)

/* ============ 可持久化配置参数（扩展版，支持多产品） ============ */

#define W25Q_CONFIG_HEADER      0xA5A5U
#define W25Q_CONFIG_VERSION     2U

typedef struct {
    uint16_t header;                       /* 0xA5A5 */
    uint16_t version;                      /* 配置版本号 */
    uint8_t  product_type;                 /* 产品类型: small_scissor 固定为 1 */
    uint8_t  reserved1;                    /* 对齐填充 */
    uint16_t motor_to_valve_delay_ms;      /* 电机到气阀延时，默认 200 */
    uint16_t motor_hold_ms;                /* 主机下降电机保持，默认 3000 */
    uint16_t sub_motor_hold_ms;            /* 子机下降电机保持，默认 1500 */
    uint8_t  module_enable_mask;           /* 模块使能位图: bit0 蜂鸣器 bit1 压力 bit2 RS485 bit3 温度 */
    uint8_t  reserved2;                    /* 对齐填充 */
    uint16_t photoelectric_debounce_ms;    /* 光电去抖，默认 50 */
    uint16_t estop_debounce_ms;            /* 急停去抖，默认 20 */
    /* 旧双柱字段（保留兼容，大剪不使用） */
    uint16_t tolerance_up;                 /* 上升允差（脉冲数） */
    uint16_t tolerance_down;               /* 下降允差（脉冲数） */
    uint16_t stall_timeout_ms;             /* 堵转判断时间 */
    uint16_t balance_wait_max_ms;          /* 平衡等待超时 */
    uint16_t collision_debounce_ms;        /* 防碰去抖时间 */
    uint16_t secondary_descent_pulses;     /* 二次下降高度（脉冲数） */
    uint8_t  dual_column_mode;             /* 0=独立 1=双柱联动 */
    uint8_t  screw_lead_mm;                /* 丝杆导程 */
    uint16_t max_pulses;                   /* 上限位脉冲数（到顶停机） */
    uint8_t  reserved[16];                 /* 预留扩展 */
    uint16_t crc16;                        /* CRC16 */
} w25q_config_t;

/* ============ 运行统计结构 ============ */

#define W25Q_STATS_MAGIC        0x53544154U   /* "STAT" */
#define W25Q_STATS_VERSION      1U

typedef struct {
    uint32_t magic;            /* W25Q_STATS_MAGIC */
    uint16_t version;
    uint16_t reserved;
    uint32_t up_count;         /* 上升次数 */
    uint32_t down_count;       /* 下降次数 */
    uint32_t lock_count;       /* 锁定次数 */
    uint32_t refill_count;     /* 补油次数 */
    uint32_t estop_count;      /* 急停次数 */
    uint32_t photo_alarm_count;/* 光电报警次数 */
    uint32_t total_run_ms;     /* 总运行时长 */
    uint32_t up_count_main;    /* 大剪主机上升次数 */
    uint32_t up_count_sub;     /* 大剪子机上升次数 */
    uint32_t down_count_main;  /* 大剪主机下降次数 */
    uint32_t down_count_sub;   /* 大剪子机下降次数 */
    uint32_t boot_count;       /* 开机次数 */
    uint32_t crc;              /* CRC32 */
} w25q_stats_t;

/* ============ 操作日志条目（16 字节） ============ */

typedef struct {
    uint32_t timestamp;     /* 时间戳（暂用 HAL_GetTick，后续接 RTC） */
    uint8_t  op_type;      /* op_type_t 枚举值 */
    uint8_t  op_result;    /* 0=ok 1=interrupted 2=failed */
    uint16_t duration_ms;  /* 动作持续时长 */
    uint8_t  detail[8];    /* 详情（如角色、方向等） */
} w25q_op_log_entry_t;

#define W25Q_OPLOG_ENTRY_SIZE   sizeof(w25q_op_log_entry_t)

/* ============ 高度存储结构（双柱保留） ============ */

typedef struct {
    uint32_t magic;         /* 0x48494748 = "HIGH" */
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    int32_t  heights[2];    /* 两个立柱脉冲计数 */
    uint32_t crc;           /* CRC32 */
} w25q_height_t;

#define W25Q_HEIGHT_MAGIC   0x48494748U
#define W25Q_HEIGHT_VERSION 2U

/* ============ 全局对象 ============ */

extern w25q_t W25Q_Flash;
extern w25q_storage_t g_w25q_storage;
extern w25q_config_t g_config;
extern w25q_stats_t g_stats;

/* ============ 系统初始化 ============ */
void App_W25Qxx_System_Init(void);

/* ============ Storage 接口 ============ */
uint32_t App_W25Qxx_Get_JEDEC_ID(void);
uint8_t App_W25Qxx_Storage_Save(void);
void App_W25Qxx_Storage_Load(void);

/* ============ Config 接口 ============ */
void App_W25Qxx_Config_Load(void);
uint8_t App_W25Qxx_Config_Save(void);

/* ============ Stats 接口 ============ */
void App_W25Qxx_Stats_Load(void);
uint8_t App_W25Qxx_Stats_Save(void);
void App_W25Qxx_Stats_Inc_Up(lift_role_t role);
void App_W25Qxx_Stats_Inc_Down(lift_role_t role);
void App_W25Qxx_Stats_Inc_Lock(void);
void App_W25Qxx_Stats_Inc_Refill(void);
void App_W25Qxx_Stats_Inc_Estop(void);
void App_W25Qxx_Stats_Inc_PhotoAlarm(void);
void App_W25Qxx_Stats_Add_RunMs(uint32_t ms);

/* ============ OpLog 接口 ============ */
uint8_t App_W25Qxx_OpLog_Append(const w25q_op_log_entry_t *entry);
uint8_t App_W25Qxx_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out);
uint16_t App_W25Qxx_OpLog_Count(void);
void App_W25Qxx_OpLog_Clear(void);

/* ============ Height 接口（双柱保留） ============ */
void    App_W25Qxx_Height_Load(void);
uint8_t App_W25Qxx_Height_Save(void);
uint8_t App_W25Qxx_Height_Is_Loaded(void);
void    App_W25Qxx_Height_Save_If_Needed(void);

/* ============ 高度脉冲 → mm 换算宏 ============ */
#define HEIGHT_MM(p)  ((int32_t)(p) * (int32_t)g_config.screw_lead_mm)

#ifdef __cplusplus
}
#endif

#endif /* __APP_W25QXX_H__ */
