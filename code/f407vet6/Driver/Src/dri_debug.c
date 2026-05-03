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

    #if W25Qxx_DEBUG_MODE == 1
    elog_i("DBG", "Counter restored: %lu", g_blink_counter);
    #endif

    while (1)
    {
        // LED 翻转
        HAL_GPIO_TogglePin(LED_DEBUG_PORT, LED_DEBUG_PIN);

        // 点亮时计数 +1
        if (HAL_GPIO_ReadPin(LED_DEBUG_PORT, LED_DEBUG_PIN) == GPIO_PIN_SET)
        {
            g_blink_counter++;

            // copy 到全局存储区，再写回 Flash
            g_w25q_storage.debug_counter = g_blink_counter;
            if (App_W25Qxx_Storage_Save() != W25Q_OK)
            {
                elog_e("DBG", "Save to W25Q FAILED");
            }

            #if W25Qxx_DEBUG_MODE == 1
            elog_i("DBG", "Blink count: %lu", g_blink_counter);
            #endif
        }

        osDelay(LED_BLINK_MS);
    }
}
