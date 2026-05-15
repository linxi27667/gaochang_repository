#include "motor.h"
#include "safety.h"
#include "app_w25qxx.h"
#include "cmsis_os.h"

#if MOTOR_DEBUG == 1
#include "elog.h"
#endif

motor_column_t g_column[2] = {0};

static GPIO_TypeDef* GetColPort(uint8_t col) {
    return COL_MAIN_PORT(col);
}
static uint16_t GetColPin(uint8_t col) {
    return COL_MAIN_PIN(col);
}

void Motor_Init(void)
{
    g_column[0].motor_state    = MOTOR_STOPPED;
    g_column[0].counting_enable = 0;
    g_column[0].pulse_count     = 0;
    g_column[1].motor_state    = MOTOR_STOPPED;
    g_column[1].counting_enable = 0;
    g_column[1].pulse_count     = 0;
}

void Motor_Start(uint8_t column_index, direction_t direction)
{
    if (g_safety.alarm != ALARM_NONE)            return;
    if (direction == DIR_UP && g_safety.at_upper_limit)  return;
    if (direction == DIR_DOWN && g_safety.at_lower_limit)return;
    if (g_safety.stall_suspected)                return;

    if (direction == DIR_UP) {
        HAL_GPIO_WritePin(RELAY_DOWN_PORT, RELAY_DOWN_PIN, RELAY_OFF);
        HAL_GPIO_WritePin(RELAY_UP_PORT,   RELAY_UP_PIN,   RELAY_ON);
        osDelay(20);
        HAL_GPIO_WritePin(GetColPort(column_index), GetColPin(column_index), RELAY_ON);
    } else if (direction == DIR_DOWN) {
        HAL_GPIO_WritePin(RELAY_UP_PORT,   RELAY_UP_PIN,   RELAY_OFF);
        HAL_GPIO_WritePin(RELAY_DOWN_PORT, RELAY_DOWN_PIN, RELAY_ON);
        osDelay(20);
        HAL_GPIO_WritePin(GetColPort(column_index), GetColPin(column_index), RELAY_ON);
    } else {
        return;
    }

    g_column[column_index].motor_state    = MOTOR_RUNNING;
    g_column[column_index].counting_enable = 1;
    g_safety.last_pulse_tick[column_index] = HAL_GetTick();

    #if MOTOR_DEBUG == 1
    elog_i("MOTOR", "START col=%d dir=%s", column_index, direction==DIR_UP?"UP":"DOWN");
    #endif
}

void Motor_Stop(uint8_t column_index)
{
    HAL_GPIO_WritePin(GetColPort(column_index), GetColPin(column_index), RELAY_OFF);
    osDelay(10);

    g_column[column_index].motor_state    = MOTOR_STOPPED;
    g_column[column_index].counting_enable = 0;

    #if MOTOR_DEBUG == 1
    elog_i("MOTOR", "STOP col=%d", column_index);
    #endif
}

void Motor_Pause(uint8_t column_index)
{
    HAL_GPIO_WritePin(GetColPort(column_index), GetColPin(column_index), RELAY_OFF);

    g_column[column_index].motor_state    = MOTOR_STOPPED;
    g_column[column_index].counting_enable = 0;
}

void Motor_Stop_All(void)
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state == MOTOR_RUNNING
            && (now - g_safety.last_pulse_tick[i]) > g_config.stall_timeout_ms) {
            g_safety.stall_suspected = 1;
        }
    }

    HAL_GPIO_WritePin(RELAY_UP_PORT,   RELAY_UP_PIN,   RELAY_OFF);
    HAL_GPIO_WritePin(RELAY_DOWN_PORT, RELAY_DOWN_PIN, RELAY_OFF);
    osDelay(10);
    HAL_GPIO_WritePin(COL_LEFT_MAIN_PORT,  COL_LEFT_MAIN_PIN,  RELAY_OFF);
    HAL_GPIO_WritePin(COL_RIGHT_MAIN_PORT, COL_RIGHT_MAIN_PIN, RELAY_OFF);

    g_column[0].motor_state    = MOTOR_STOPPED;
    g_column[0].counting_enable = 0;
    g_column[1].motor_state    = MOTOR_STOPPED;
    g_column[1].counting_enable = 0;

    #if MOTOR_DEBUG == 1
    elog_i("MOTOR", "STOP ALL");
    #endif
}

void Motor_Start_All(direction_t direction)
{
    if (g_safety.alarm != ALARM_NONE)            return;
    if (direction == DIR_UP && g_safety.at_upper_limit)  return;
    if (direction == DIR_DOWN && g_safety.at_lower_limit)return;
    if (g_safety.stall_suspected)                return;

    if (direction == DIR_UP) {
        HAL_GPIO_WritePin(RELAY_DOWN_PORT, RELAY_DOWN_PIN, RELAY_OFF);
        HAL_GPIO_WritePin(RELAY_UP_PORT,   RELAY_UP_PIN,   RELAY_ON);
        osDelay(20);
        HAL_GPIO_WritePin(COL_LEFT_MAIN_PORT,  COL_LEFT_MAIN_PIN,  RELAY_ON);
        HAL_GPIO_WritePin(COL_RIGHT_MAIN_PORT, COL_RIGHT_MAIN_PIN, RELAY_ON);

        g_column[0].motor_state = MOTOR_RUNNING;
        g_column[0].counting_enable = 1;
        g_column[1].motor_state = MOTOR_RUNNING;
        g_column[1].counting_enable = 1;
    } else if (direction == DIR_DOWN) {
        HAL_GPIO_WritePin(RELAY_UP_PORT,   RELAY_UP_PIN,   RELAY_OFF);
        HAL_GPIO_WritePin(RELAY_DOWN_PORT, RELAY_DOWN_PIN, RELAY_ON);
        osDelay(20);
        HAL_GPIO_WritePin(COL_LEFT_MAIN_PORT,  COL_LEFT_MAIN_PIN,  RELAY_ON);
        HAL_GPIO_WritePin(COL_RIGHT_MAIN_PORT, COL_RIGHT_MAIN_PIN, RELAY_ON);

        g_column[0].motor_state = MOTOR_RUNNING;
        g_column[0].counting_enable = 1;
        g_column[1].motor_state = MOTOR_RUNNING;
        g_column[1].counting_enable = 1;
    }

    g_safety.last_pulse_tick[0] = HAL_GetTick();
    g_safety.last_pulse_tick[1] = HAL_GetTick();
}

void Motor_Stop_All_Immediate(void)
{
    HAL_GPIO_WritePin(RELAY_UP_PORT,   RELAY_UP_PIN,   RELAY_OFF);
    HAL_GPIO_WritePin(RELAY_DOWN_PORT, RELAY_DOWN_PIN, RELAY_OFF);
    HAL_GPIO_WritePin(COL_LEFT_MAIN_PORT,  COL_LEFT_MAIN_PIN,  RELAY_OFF);
    HAL_GPIO_WritePin(COL_RIGHT_MAIN_PORT, COL_RIGHT_MAIN_PIN, RELAY_OFF);

    g_column[0].motor_state    = MOTOR_STOPPED;
    g_column[0].counting_enable = 0;
    g_column[1].motor_state    = MOTOR_STOPPED;
    g_column[1].counting_enable = 0;

    #if MOTOR_DEBUG == 1
    elog_w("MOTOR", "STOP_ALL_EMERGENCY");
    #endif
}
