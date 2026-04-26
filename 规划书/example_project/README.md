# 轻量 FreeRTOS 架构 - 丝杆举升机示例

## 三层结构

```
┌─────────────────────────────────────────────────┐
│  Drivers/  (驱动层)                              │
│  drv_gpio.h   → 引脚宏：E_STOP_Read(), KM1_RISE_ON() │
│  drv_gpio.c   → GPIO 初始化                      │
│  drv_tim.c    → TIM5 输入捕获 + 中断发通知        │
├─────────────────────────────────────────────────┤
│  App/       (业务层)                              │
│  app_motor.c  → Motor_Task   10ms 状态机         │
│  app_sensor.c → Sensor_Task  等中断通知，计圈     │
│  app_sync.c   → Sync_Task    50ms 4圈误差检查     │
│  app_alarm.c  → Alarm_Task   100ms 堵转检测       │
│  app_comm.c   → Comm_Task    200ms HMI 上报       │
├─────────────────────────────────────────────────┤
│  main.c     (入口)                               │
│  硬件初始化 + 创建互斥量 + 创建任务 + 启动调度器   │
└─────────────────────────────────────────────────┘
```

## 关键设计点

### 1. 引脚宏封装（drv_gpio.h）
业务层不写 `HAL_GPIO_WritePin`，而是用宏：
```c
if (E_STOP_Read() == GPIO_PIN_RESET) { ... }
KM1_RISE_ON();
```
改引脚只改头文件，业务代码不动。

### 2. 双柱对称对象用数组
```c
Column_t cols[2];  // cols[0]=柱1, cols[1]=柱2
```
共享资源用互斥量保护：
```c
xSemaphoreTake(hColsMutex, portMAX_DELAY);
// 读写 cols[]
xSemaphoreGive(hColsMutex);
```

### 3. 中断 → 任务通信用任务通知
ISR 里不做业务逻辑，只发通知：
```c
// drv_tim.c 中断回调
xTaskNotifyFromISR(hSensorTask, 1, eSetValueWithOverwrite, &hpwp);
```
任务端阻塞等待：
```c
uint32_t value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
```

### 4. 每个任务用 vTaskDelay 控制周期
```c
Motor_Task  → vTaskDelay(pdMS_TO_TICKS(10))    // 10ms
Sync_Task   → vTaskDelay(pdMS_TO_TICKS(50))    // 50ms
Alarm_Task  → vTaskDelay(pdMS_TO_TICKS(100))   // 100ms
Comm_Task   → vTaskDelay(pdMS_TO_TICKS(200))   // 200ms
```

### 5. 优先级分配
| 优先级 | 任务 | 说明 |
|--------|------|------|
| 4 | Emergency | 最高应用优先级 |
| 3 | Motor, Sync | 核心控制 |
| 2 | Sensor, Alarm | 传感器/报警 |
| 1 | Comm | 通信上报 |

## 扩展指南
- **加新功能**：在 App/ 中新建 `app_xxx.c/.h`，写任务函数，在 main.c 中 `xTaskCreate`
- **加引脚**：在 `drv_gpio.h` 中加宏定义
- **加通信协议**：在 Comm/ 中新建文件，被 Comm_Task 调用
