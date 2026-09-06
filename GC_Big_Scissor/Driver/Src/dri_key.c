#include "dri_key.h"
#include "key.h"
#include "FreeRTOS.h"
#include "task.h"
#include "elog.h"

void Key_Task_Create(void)
{
    xTaskCreate(Key_Task, "key", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
}

void Key_Task(void *pvParameters)
{
    (void)pvParameters;
    uint8_t last_up = 0xFF;
    uint8_t last_down = 0xFF;

    while (1)
    {
        Key_Scan();

        if (last_up != g_command.button_up || last_down != g_command.button_down) {
            last_up = g_command.button_up;
            last_down = g_command.button_down;
            elog_i("KEY", "[KEY] state up=%d down=%d", last_up, last_down);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
