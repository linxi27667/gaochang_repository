#include "dri_motor.h"
#include "key.h"
#include "safety.h"
#include "app_w25qxx.h"
#include "cmsis_os.h"

void Control_Task_Create(void)
{
    xTaskCreate(Control_Task, "control", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

void Control_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        if (Safety_Alarm_Handle()) { osDelay(10); continue; }

        Key_Jog_Conflict_Check();
        Key_Jog_Release_Check();
        Key_Jog_Start_Check();

        Safety_Running_Update();

        App_W25Qxx_Height_Save_If_Needed();

        osDelay(10);
    }
}
