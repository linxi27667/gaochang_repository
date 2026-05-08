/* ===================================================================
 * 丝杆举升机 PC 模拟测试
 * 编译:
 *   gcc -std=c11 -I ../APP/Inc -I ../Driver/Inc -I ../BSP/Inc -I ../Core/Inc -I .
 *       main.c stm32f4xx_hal.c bsp_w25qxx.c app_spi.c elog.c SEGGER_RTT.c
 *       cmsis_os.c mx_init.c
 *       ../APP/Src/motor.c ../APP/Src/key.c ../APP/Src/encoder.c
 *       ../APP/Src/balance.c ../APP/Src/safety.c ../APP/Src/app_w25qxx.c
 *       -o sim_test && ./sim_test
 * =================================================================== */

#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "motor.h"
#include "key.h"
#include "safety.h"
#include "encoder.h"
#include "balance.h"
#include "app_w25qxx.h"

/* ==================== 测试辅助 ==================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); tests_passed++; } \
    else { printf("  [FAIL] %s (line %d)\n", msg, __LINE__); tests_failed++; } \
} while(0)

#define CHECK_EQ(a, b, msg) do { \
    int _av = (int)(a), _bv = (int)(b); \
    if (_av == _bv) { printf("  [PASS] %s (=%d)\n", msg, _av); tests_passed++; } \
    else { printf("  [FAIL] %s: expected %d got %d (line %d)\n", msg, _bv, _av, __LINE__); tests_failed++; } \
} while(0)

/*
 * 模拟一个完整的控制周期。
 * 注意：不能直接调用 Control_Task/Safety_Task/Key_Task
 * 因为它们是 while(1) 无限循环。这里把核心逻辑提取出来。
 */
static void sim_control_cycle(void)
{
    /* --- Key_Task 逻辑 --- */
    Key_Scan();
    Buzzer_Poll();

    /* --- Control_Task 逻辑 --- */
    if (g_safety.alarm_state == ALARM_NONE) {

        if (g_command.button_stop && g_command.direction != DIR_STOP) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
        }
        else if (g_command.button_up && !g_command.button_down
                 && g_command.direction == DIR_STOP) {
            Motor_Start_All(DIR_UP);
            g_command.direction = DIR_UP;
        }
        else if (g_command.button_down && !g_command.button_up
                 && g_command.direction == DIR_STOP) {

            if (g_safety.secondary_descent_triggered
                && !g_safety.secondary_descent_confirmed) {
                Buzzer_Beep(2000);
            } else {
                Motor_Start_All(DIR_DOWN);
                g_command.direction = DIR_DOWN;
                Buzzer_Beep(500);
            }
        }

        if (g_command.direction != DIR_STOP) {
            Balance_Run();
            Safety_Check_Secondary_Descent();
        }
    }

    /* --- Safety_Task 逻辑 --- */
    Safety_Check_Stall();

    if (g_command.button_confirm && g_safety.secondary_descent_triggered) {
        g_safety.secondary_descent_confirmed = 1;
    }

    if (g_safety.alarm_state != ALARM_NONE && g_command.button_stop) {
        Safety_Alarm_Reset();
    }
}

/* ==================== 测试用例 ==================== */

static void test_01_power_on_safe_state(void)
{
    printf("\n--- Test 01: 上电安全态 ---\n");

    Motor_Init();
    Safety_Init();
    Key_Init();

    CHECK_EQ(sim_pin_read(COL_LEFT_MAIN_PORT,  COL_LEFT_MAIN_PIN),  0, "COL_LEFT_MAIN OFF");
    CHECK_EQ(sim_pin_read(COL_RIGHT_MAIN_PORT, COL_RIGHT_MAIN_PIN), 0, "COL_RIGHT_MAIN OFF");
    CHECK_EQ(sim_pin_read(RELAY_UP_PORT,       RELAY_UP_PIN),       0, "RELAY_UP OFF");
    CHECK_EQ(sim_pin_read(RELAY_DOWN_PORT,     RELAY_DOWN_PIN),     0, "RELAY_DOWN OFF");
    CHECK_EQ(sim_pin_read(BUZZER_PORT, BUZZER_PIN),           0, "BUZZER OFF");
    CHECK_EQ(g_safety.alarm_state, ALARM_NONE, "Alarm NONE");
}

static void test_02_button_up_starts_motor(void)
{
    printf("\n--- Test 02: 上升按钮启动电机 ---\n");

    Motor_Init();
    Safety_Init();
    Key_Init();
    g_command.direction = DIR_STOP;

    sim_button_press(BUTTON_UP_PIN);
    sim_control_cycle();

    CHECK_EQ(sim_pin_read(RELAY_UP_PORT,       RELAY_UP_PIN),       1, "RELAY_UP ON");
    CHECK_EQ(sim_pin_read(COL_LEFT_MAIN_PORT,  COL_LEFT_MAIN_PIN),  1, "COL_LEFT_MAIN ON");
    CHECK_EQ(g_column[0].motor_state, MOTOR_RUNNING, "Column 0 RUNNING");
    CHECK_EQ(g_command.direction, DIR_UP, "Direction UP");

    sim_button_release(BUTTON_UP_PIN);
}

static void test_03_encoder_counts_pulses(void)
{
    printf("\n--- Test 03: 编码器脉冲计数 ---\n");

    Motor_Init();
    Safety_Init();
    g_column[0].counting_enable = 1;
    g_column[0].pulse_count = 0;

    sim_encoder_pulse(1);
    sim_encoder_pulse(1);
    sim_encoder_pulse(1);

    CHECK_EQ(Encoder_Get_Count(0), 3, "Count after 3 pulses");
    CHECK(g_safety.last_pulse_tick[0] > 0, "last_pulse_tick updated");
}

static void test_04_balance_pauses_fast_column(void)
{
    printf("\n--- Test 04: 双柱平衡-快柱暂停 ---\n");

    Motor_Init();
    Safety_Init();
    g_command.direction = DIR_UP;
    g_config.dual_column_mode = 1;
    g_config.tolerance_up = 4;

    g_column[0].pulse_count = 5;  g_column[0].motor_state = MOTOR_RUNNING;
    g_column[1].pulse_count = 0;  g_column[1].motor_state = MOTOR_RUNNING;

    Balance_Run();

    CHECK_EQ(g_column[0].motor_state, MOTOR_WAITING_BALANCE, "Column 0 paused (faster)");
    CHECK_EQ(g_column[1].motor_state, MOTOR_RUNNING, "Column 1 still running");
}

static void test_05_balance_resumes_when_caught_up(void)
{
    printf("\n--- Test 05: 双柱平衡-慢柱追上后恢复 ---\n");

    Motor_Init();
    Safety_Init();
    g_command.direction = DIR_UP;
    g_config.dual_column_mode = 1;
    g_config.tolerance_up = 4;

    g_column[0].pulse_count = 5;  g_column[0].motor_state = MOTOR_WAITING_BALANCE;
    g_column[1].pulse_count = 3;  g_column[1].motor_state = MOTOR_RUNNING;

    Balance_Run();

    CHECK_EQ(g_column[0].motor_state, MOTOR_RUNNING, "Column 0 resumed");
}

static void test_06_button_down_reverse_and_buzzer(void)
{
    printf("\n--- Test 06: 下降-反转+蜂鸣器 ---\n");

    Motor_Init();
    Safety_Init();
    Key_Init();
    g_safety.secondary_descent_triggered = 0;
    g_command.direction = DIR_STOP;
    g_column[0].pulse_count = 100;  /* 高于二次下降阈值 */
    g_column[1].pulse_count = 100;

    sim_button_press(BUTTON_DOWN_PIN);
    sim_control_cycle();

    CHECK_EQ(sim_pin_read(RELAY_DOWN_PORT, RELAY_DOWN_PIN), 1, "RELAY_DOWN ON");
    CHECK_EQ(g_command.direction, DIR_DOWN, "Direction DOWN");
    CHECK_EQ(sim_pin_read(BUZZER_PORT, BUZZER_PIN), 1, "BUZZER ON when descending");

    sim_button_release(BUTTON_DOWN_PIN);
}

static void test_07_button_stop_motors(void)
{
    printf("\n--- Test 07: 停止按钮停机 ---\n");

    Motor_Init();
    Safety_Init();
    Key_Init();

    g_command.direction = DIR_STOP;
    sim_button_press(BUTTON_UP_PIN);
    sim_control_cycle();
    sim_button_release(BUTTON_UP_PIN);
    CHECK_EQ(g_command.direction, DIR_UP, "Started UP");

    sim_button_press(BUTTON_STOP_PIN);
    sim_control_cycle();
    sim_button_release(BUTTON_STOP_PIN);

    CHECK_EQ(g_command.direction, DIR_STOP, "Direction STOP");
    CHECK_EQ(sim_pin_read(RELAY_UP_PORT,        RELAY_UP_PIN),        0, "RELAY_UP OFF");
    CHECK_EQ(sim_pin_read(RELAY_DOWN_PORT,      RELAY_DOWN_PIN),      0, "RELAY_DOWN OFF");
    CHECK_EQ(sim_pin_read(COL_LEFT_MAIN_PORT,   COL_LEFT_MAIN_PIN),   0, "COL_LEFT_MAIN OFF");
}

static void test_08_collision_immediate_stop(void)
{
    printf("\n--- Test 08: 防碰杆立即停机 ---\n");

    Motor_Init();
    Safety_Init();
    g_column[0].motor_state = MOTOR_RUNNING;
    g_column[1].motor_state = MOTOR_RUNNING;

    sim_tick_advance(100);
    Safety_EXTI_Handler(COLLISION_1_PIN);

    CHECK_EQ(g_safety.alarm_state, ALARM_COLLISION, "Alarm COLLISION");
    CHECK_EQ(g_column[0].motor_state, MOTOR_STOPPED, "Column 0 STOPPED");
    CHECK_EQ(g_column[1].motor_state, MOTOR_STOPPED, "Column 1 STOPPED");
    CHECK_EQ(sim_pin_read(BUZZER_PORT, BUZZER_PIN), 1, "BUZZER ON");
}

static void test_09_lower_limit_resets_counters(void)
{
    printf("\n--- Test 09: 下限位清零 ---\n");

    g_column[0].pulse_count = 100;
    g_column[1].pulse_count = 100;

    Safety_EXTI_Handler(LOWER_LIMIT_PIN);

    CHECK_EQ(Encoder_Get_Count(0), 0, "Column 0 reset");
    CHECK_EQ(Encoder_Get_Count(1), 0, "Column 1 reset");
    CHECK_EQ(g_safety.at_lower_limit, 1, "at_lower_limit set");
}

static void test_10_stall_detection(void)
{
    printf("\n--- Test 10: 堵转检测 ---\n");

    Motor_Init();
    Safety_Init();
    g_column[0].motor_state = MOTOR_RUNNING;
    g_safety.last_pulse_tick[0] = 1000;
    sim_tick_advance(3000);

    Safety_Check_Stall();

    CHECK_EQ(g_safety.alarm_state, ALARM_STALL, "Alarm STALL");
    CHECK_EQ(g_column[0].motor_state, MOTOR_STOPPED, "Motor stopped");
}

static void test_11_alarm_reset(void)
{
    printf("\n--- Test 11: 告警复位 ---\n");

    Safety_Init();
    g_safety.alarm_state = ALARM_COLLISION;
    g_safety.collision_triggered = 1;
    Buzzer_On();

    sim_button_press(BUTTON_STOP_PIN);
    sim_control_cycle();
    sim_button_release(BUTTON_STOP_PIN);

    CHECK_EQ(g_safety.alarm_state, ALARM_NONE, "Alarm NONE");
    CHECK_EQ(g_command.direction, DIR_STOP, "Direction STOP");
    CHECK_EQ(sim_pin_read(BUZZER_PORT, BUZZER_PIN), 0, "Buzzer OFF");
}

static void test_12_height_save_load(void)
{
    printf("\n--- Test 12: 高度 Flash 存储往返 ---\n");

    g_column[0].pulse_count = 42;
    g_column[1].pulse_count = 73;

    uint8_t r = App_W25Qxx_Height_Save();
    CHECK_EQ(r, W25Q_OK, "Height save OK");

    g_column[0].pulse_count = 0;
    g_column[1].pulse_count = 0;

    App_W25Qxx_Height_Load();
    CHECK_EQ(g_column[0].pulse_count, 42, "Column 0 restored");
    CHECK_EQ(g_column[1].pulse_count, 73, "Column 1 restored");
}

static void test_13_secondary_descent(void)
{
    printf("\n--- Test 13: 二次下降保护 ---\n");

    Motor_Init();
    Safety_Init();
    g_command.direction = DIR_DOWN;
    g_config.secondary_descent_pulses = 30;
    g_column[0].pulse_count = 20;
    g_column[1].pulse_count = 25;

    Safety_Check_Secondary_Descent();

    CHECK_EQ(g_safety.secondary_descent_triggered, 1, "Secondary descent triggered");
    CHECK_EQ(g_column[0].motor_state, MOTOR_STOPPED, "Motor stopped");
}

static void test_14_single_column_mode(void)
{
    printf("\n--- Test 14: 单柱模式不触发平衡 ---\n");

    Motor_Init();
    Safety_Init();
    g_config.dual_column_mode = 0;
    g_command.direction = DIR_UP;
    g_column[0].pulse_count = 100;  g_column[0].motor_state = MOTOR_RUNNING;
    g_column[1].pulse_count = 0;    g_column[1].motor_state = MOTOR_RUNNING;

    Balance_Run();

    CHECK_EQ(g_column[0].motor_state, MOTOR_RUNNING, "Column 0 still running");
    g_config.dual_column_mode = 1;  /* 恢复默认 */
}

static void test_15_upper_limit_stops_motor(void)
{
    printf("\n--- Test 15: 上限位到顶停机 ---\n");

    Motor_Init();
    Safety_Init();
    g_command.direction = DIR_UP;
    g_config.max_pulses = 100;
    g_column[0].pulse_count = 99;  g_column[0].motor_state = MOTOR_RUNNING;
    g_column[1].pulse_count = 99;  g_column[1].motor_state = MOTOR_RUNNING;

    Safety_Check_Upper_Limit();

    CHECK_EQ(g_column[0].motor_state, MOTOR_RUNNING, "Column 0 still running (99 < 100)");

    /* 再走一个脉冲到顶 */
    g_column[0].pulse_count = 100;
    Safety_Check_Upper_Limit();

    CHECK_EQ(g_safety.alarm_state, ALARM_UPPER_LIMIT, "Upper limit alarm");
    CHECK_EQ(g_column[0].motor_state, MOTOR_STOPPED, "Column 0 stopped at max");
}

static void test_16_lower_limit_stops_descent(void)
{
    printf("\n--- Test 16: 下限到底停机 ---\n");

    Motor_Init();
    Safety_Init();
    g_command.direction = DIR_DOWN;
    g_column[0].pulse_count = 1;  g_column[0].motor_state = MOTOR_RUNNING;
    g_column[1].pulse_count = 1;  g_column[1].motor_state = MOTOR_RUNNING;

    Safety_Check_Lower_Limit();

    CHECK_EQ(g_column[0].motor_state, MOTOR_RUNNING, "Still running (1 > 0)");

    /* 到0 */
    g_column[0].pulse_count = 0;
    Safety_Check_Lower_Limit();

    CHECK_EQ(g_safety.at_lower_limit, 1, "Lower limit flag");
    CHECK_EQ(g_column[0].motor_state, MOTOR_STOPPED, "Stopped at 0");
}

/* ==================== 入口 ==================== */

int main(void)
{
    printf("========================================\n");
    printf("  丝杆举升机 STM32 逻辑模拟测试\n");
    printf("========================================\n");

    App_W25Qxx_System_Init();
    Motor_Init();
    Safety_Init();
    Key_Init();
    Encoder_Init();

    /* 初始化所有按键为释放状态（上拉高电平） */
    sim_button_release(BUTTON_UP_PIN);
    sim_button_release(BUTTON_DOWN_PIN);
    sim_button_release(BUTTON_STOP_PIN);
    sim_button_release(BUTTON_CONFIRM_PIN);

    test_01_power_on_safe_state();
    test_02_button_up_starts_motor();
    test_03_encoder_counts_pulses();
    test_04_balance_pauses_fast_column();
    test_05_balance_resumes_when_caught_up();
    test_06_button_down_reverse_and_buzzer();
    test_07_button_stop_motors();
    test_08_collision_immediate_stop();
    test_09_lower_limit_resets_counters();
    test_10_stall_detection();
    test_11_alarm_reset();
    test_12_height_save_load();
    test_13_secondary_descent();
    test_14_single_column_mode();
    test_15_upper_limit_stops_motor();
    test_16_lower_limit_stops_descent();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed ? 1 : 0;
}
