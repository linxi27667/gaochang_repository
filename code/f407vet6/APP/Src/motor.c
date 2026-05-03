#include "motor.h"
#include "safety.h"
#include "cmsis_os.h"

#if MOTOR_DEBUG == 1
#include "elog.h"
#endif

/* ==================== 全局变量 ==================== */

motor_column_t g_column[2] = {0};

/* ==================== 电机初始化 ==================== */

void Motor_Init(void)
{
    Motor_Stop_All();
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, BRAKE_HOLD);
}

/* ==================== 启动单柱电机 ==================== */

void Motor_Start(uint8_t column_index, uint8_t direction)
{
    if (direction == DIR_UP) {
        /* 上升：主接触器合 → 延时 → 上升接触器合 */
        if (column_index == 0) {
            HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, RELAY_ON);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, RELAY_ON);
        } else {
            HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, RELAY_ON);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, RELAY_ON);
        }
    } else if (direction == DIR_DOWN) {
        /* 下降：反转继电器合 → 延时 → 主接触器合 → 延时 → 上升接触器合 */
        HAL_GPIO_WritePin(REVERSE_PORT, REVERSE_PIN, RELAY_ON);
        osDelay(20);
        if (column_index == 0) {
            HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, RELAY_ON);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, RELAY_ON);
        } else {
            HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, RELAY_ON);
            osDelay(20);
            HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, RELAY_ON);
        }
    } else {
        return;
    }

    /* 通电释放刹车 */
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, BRAKE_RELEASE);

    g_column[column_index].motor_state    = MOTOR_RUNNING;
    g_column[column_index].counting_enable = 1;

    /* 初始化堵转检测时间基准，防止启动瞬间误报 */
    g_safety.last_pulse_tick[column_index] = HAL_GetTick();

    #if MOTOR_DEBUG == 1
    elog_i("MOTOR", "START col=%d dir=%s", column_index, direction==DIR_UP?"UP":"DOWN");
    #endif
}

/* ==================== 停止单柱电机 ==================== */

void Motor_Stop(uint8_t column_index)
{
    /* 先关上升接触器 → 关主接触器 */
    if (column_index == 0) {
        HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, RELAY_OFF);
        osDelay(10);
        HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, RELAY_OFF);
    } else {
        HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, RELAY_OFF);
        osDelay(10);
        HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, RELAY_OFF);
    }

    /* 关反转、抱紧刹车 */
    HAL_GPIO_WritePin(REVERSE_PORT, REVERSE_PIN, RELAY_OFF);
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, BRAKE_HOLD);

    g_column[column_index].motor_state    = MOTOR_STOPPED;
    g_column[column_index].counting_enable = 0;

    #if MOTOR_DEBUG == 1
    elog_i("MOTOR", "STOP col=%d", column_index);
    #endif
}

/* ==================== 停止所有电机 ==================== */

void Motor_Stop_All(void)
{
    Motor_Stop(0);
    Motor_Stop(1);
}

/* ==================== 启动所有电机 ==================== */

void Motor_Start_All(uint8_t direction)
{
    Motor_Start(0, direction);
    Motor_Start(1, direction);
}

/* ==================== ISR 安全紧急停机（无延时） ==================== */

void Motor_Stop_All_Immediate(void)
{
    /* 关两路上升接触器 */
    HAL_GPIO_WritePin(MOTOR1_UP_PORT, MOTOR1_UP_PIN, RELAY_OFF);
    HAL_GPIO_WritePin(MOTOR2_UP_PORT, MOTOR2_UP_PIN, RELAY_OFF);

    /* 关两路主接触器 */
    HAL_GPIO_WritePin(MOTOR1_MAIN_PORT, MOTOR1_MAIN_PIN, RELAY_OFF);
    HAL_GPIO_WritePin(MOTOR2_MAIN_PORT, MOTOR2_MAIN_PIN, RELAY_OFF);

    /* 关反转、抱紧刹车 */
    HAL_GPIO_WritePin(REVERSE_PORT, REVERSE_PIN, RELAY_OFF);
    HAL_GPIO_WritePin(BRAKE_PORT, BRAKE_PIN, BRAKE_HOLD);

    g_column[0].motor_state    = MOTOR_STOPPED;
    g_column[0].counting_enable = 0;
    g_column[1].motor_state    = MOTOR_STOPPED;
    g_column[1].counting_enable = 0;

    #if MOTOR_DEBUG == 1
    elog_w("MOTOR", "STOP_ALL_EMERGENCY");
    #endif
}
