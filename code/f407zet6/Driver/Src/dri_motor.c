#include "dri_motor.h"
#include "key.h"
#include "safety.h"
#include "motor.h"
#include "app_w25qxx.h"
#include "cmsis_os.h"

#if CTRL_DEBUG == 1
#include "elog.h"
#endif

void Control_Task_Create(void)
{
    xTaskCreate(Control_Task, "control", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

/* 主控制任务 — 10ms周期，举升机全部运动逻辑的调度中心
 *
 * 每周期执行顺序：
 *   1. 告警检查：有告警则跳过所有操作（仅碰撞告警可降键退出）
 *   2. 按键冲突：同时按上升+下降 → 立即停止
 *   3. 松手停止：松开按键或按下停止键 → 停止
 *   4. 按下启动：按上升/下降 → 双柱同时启动
 *   5. 运行保护：平衡检测、上限位、下限位
 *   6. 状态日志：运动中实时打印高度(每10ms)
 *   7. 心跳日志：每5秒汇总(500次×10ms=5000ms)
 *   8. 运行灯：每500ms翻转(50次×10ms) */
void Control_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t hb_cnt = 0;
    static uint32_t led_cnt = 0;

    while (1)
    {
        /* 告警状态处理 */
        if (Safety_Alarm_Handle()) { osDelay(10); continue; }

        /* 按键逻辑：冲突检测 → 松手停止 → 按下启动 */
        Key_Jog_Conflict_Check();
        Key_Jog_Release_Check();
        Key_Jog_Start_Check();

        /* 运行中保护：平衡同步、上限位、下限位 */
        Safety_Running_Update();

        App_W25Qxx_Height_Save_If_Needed();

        /* 运动中实时打印高度 */
        #if CTRL_DEBUG == 1
        if (g_command.direction != DIR_STOP) {
            elog_i("HEIGHT", "left=%ldmm right=%ldmm",
                   g_column[0].pulse_count * SCREW_LEAD_MM,
                   g_column[1].pulse_count * SCREW_LEAD_MM);
        }
        #endif

        /* LED与心跳计数 */
        hb_cnt++;
        led_cnt++;
        if (led_cnt >= 50) {
            led_cnt = 0;
            LED_RUN_TOGGLE();
        }
        if (hb_cnt >= 500) {
            hb_cnt = 0;
            const char *dir = "STOP";
            if (g_command.direction == DIR_UP)   dir = "UP";
            if (g_command.direction == DIR_DOWN) dir = "DOWN";
            elog_i("CTRL", "heartbeat %s left=%ldmm right=%ldmm",
                   dir,
                   g_column[0].pulse_count * SCREW_LEAD_MM,
                   g_column[1].pulse_count * SCREW_LEAD_MM);
        }

        osDelay(10);
    }
}
