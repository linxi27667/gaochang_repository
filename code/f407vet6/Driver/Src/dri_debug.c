#include "dri_debug.h"

/* ================= 全局变量 ================= */

// LED 闪烁计数器（模块私有静态变量）
static uint32_t g_blink_counter = 0;

/* ================= Debug Task ================= */

// LED 闪烁任务入口
void Debug_Task(void *pvParameters);

// 获取当前计数器值
uint32_t Counter_Get(void)
{
    return g_blink_counter;
}

// 创建 Debug 任务
void Debug_Task_Create(void)
{
    xTaskCreate(Debug_Task,
                "debug",
                512,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);
}

// LED 闪烁任务入口
void Debug_Task(void *pvParameters)
{
    // 上电时从全局存储区 copy 到本地静态变量
    g_blink_counter = g_w25q_storage.debug_counter;

    #if W25Q_DEBUG == 1
    elog_i("DBG", "Counter restored: %lu", g_blink_counter);
    #endif

    while (1)
    {
        /* Debug task disabled — motor control is now active */
        osDelay(1000);
    }
}
