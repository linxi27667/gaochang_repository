/*
 * app_sync.c - 双柱同步检查
 *
 * 50ms 周期：比较两柱计圈数，误差超 4 圈 → 快柱停机等待
 * 对应原 PLC "数据比较程序段"
 */
#include "app_sync.h"
#include "app_sensor.h"     /* cols[], hColsMutex */
#include "app_motor.h"      /* SystemState_t */
#include "FreeRTOS.h"
#include "task.h"

void Sync_Task(void *arg) {
    while (1) {
        xSemaphoreTake(hColsMutex, portMAX_DELAY);

        int32_t diff = cols[0].total_pulses - cols[1].total_pulses;

        if (diff > SYNC_THRESHOLD) {
            /* 柱1 快 → 停下来等柱2 */
            cols[0].sync_blocked = 1;
            cols[1].sync_blocked = 0;
        } else if (diff < -SYNC_THRESHOLD) {
            /* 柱2 快 → 停下来等柱1 */
            cols[1].sync_blocked = 1;
            cols[0].sync_blocked = 0;
        } else {
            /* 误差在允许范围内，解除锁定 */
            cols[0].sync_blocked = 0;
            cols[1].sync_blocked = 0;
        }

        xSemaphoreGive(hColsMutex);
        vTaskDelay(pdMS_TO_TICKS(50));   /* 50ms 周期 */
    }
}
