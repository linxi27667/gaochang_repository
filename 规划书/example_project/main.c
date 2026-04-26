/*
 * main.c - FreeRTOS 项目入口
 *
 * 职责：硬件初始化 + 创建 RTOS 资源 + 创建任务 + 启动调度器
 * 不写任何具体业务逻辑
 */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ===== 驱动层 ===== */
#include "drv_gpio.h"
#include "drv_tim.h"

/* ===== 业务层 ===== */
#include "app_motor.h"
#include "app_sensor.h"
#include "app_sync.h"
#include "app_alarm.h"
#include "app_comm.h"

/* ================= 任务栈和优先级配置 ================= */
#define TASK_EMERGENCY_STACK    256
#define TASK_MOTOR_STACK        512
#define TASK_SYNC_STACK         384
#define TASK_SENSOR_STACK       384
#define TASK_ALARM_STACK        384
#define TASK_COMM_STACK         512

#define PRIO_EMERGENCY          4     /* 最高应用优先级：急停 */
#define PRIO_MOTOR              3     /* 核心控制 */
#define PRIO_SYNC               3     /* 同步检查（同优先级时间片轮转） */
#define PRIO_SENSOR             2     /* 传感器（中断通知唤醒） */
#define PRIO_ALARM              2     /* 报警检测 */
#define PRIO_COMM               1     /* 通信上报 */

/* ================= 硬件初始化 ================= */
static void Hardware_Init(void) {
    HAL_Init();
    SystemClock_Config();           /* 240MHz */

    Drv_GPIO_Init();
    Drv_TIM5_Init();                /* TIM5 输入捕获，霍尔脉冲 */

    /* USART1/2 初始化（HMI / IoT） */
    /* USART1_Init(115200); */
    /* USART2_Init(115200); */
}

/* ================= RTOS 资源创建 ================= */
static void RTOS_Resources_Create(void) {
    hColsMutex = xSemaphoreCreateMutex();
}

/* ================= 任务创建 ================= */
static void Tasks_Create(void) {
    xTaskCreate(Motor_Task,     "motor",    TASK_MOTOR_STACK,     NULL, PRIO_MOTOR,    NULL);
    xTaskCreate(Sync_Task,      "sync",     TASK_SYNC_STACK,      NULL, PRIO_SYNC,     NULL);
    xTaskCreate(Sensor_Task,    "sensor",   TASK_SENSOR_STACK,    NULL, PRIO_SENSOR,   NULL);
    xTaskCreate(Alarm_Task,     "alarm",    TASK_ALARM_STACK,     NULL, PRIO_ALARM,    NULL);
    xTaskCreate(Comm_Task,      "comm",     TASK_COMM_STACK,      NULL, PRIO_COMM,     NULL);
}

/* ================= 主函数 ================= */
int main(void) {
    Hardware_Init();
    MOTOR_ALL_OFF();                    /* 确保所有输出初始为 OFF */

    RTOS_Resources_Create();
    Tasks_Create();

    vTaskStartScheduler();              /* 启动 RTOS，不再返回 */

    while (1) {                         /* 理论上不会到这里 */
    }
}

/* ================= 栈溢出钩子（必须实现） ================= */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    MOTOR_ALL_OFF();                    /* 致命错误，停机 */
    while (1);
}
