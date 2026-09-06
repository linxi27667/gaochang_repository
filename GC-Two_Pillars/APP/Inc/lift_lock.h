/**
  ******************************************************************************
  * @file    lift_lock.h
  * @brief   举升机共享变量互斥锁接口
  *
  * 设计目的：
  *   DTU 任务与 Lift_Task 之间通过 g_lift_state / g_lift_iot_status 等
  *   全局变量通信。原来的设计是单字读写硬件原子，无锁保护。
  *   现增加 FreeRTOS 互斥锁，避免多任务并发读写时出现撕裂/不一致。
  *
  * 锁内原则：
  *   - 临界区极小：仅做"读-改-写"全局标志位 / "拷贝到本地快照"
  *   - 绝不持锁调用 snprintf / elog / 任何可能的阻塞 API
  *
  * 锁对象生命周期：
  *   - s_lift_state_mutex 在 LiftLock_Init 中创建
  *   - 在 scheduler 启动前 (main.c 的 MX_FREERTOS_Init 之前) 创建
  ******************************************************************************
  */

#ifndef __LIFT_LOCK_H__
#define __LIFT_LOCK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化互斥锁（必须在 Scheduler 启动前调用，建议放在 main.c 中 osKernelStart 之前）
 */
void LiftLock_Init(void);

/* ============ 锁接口（细粒度） ============ */

/* 状态机锁：保护 g_lift_state + s_remote_locked */
void LiftLock_LockState(void);
void LiftLock_UnlockState(void);

/* IoT 状态锁：保护 g_lift_iot_status + g_iot_event_pending */
void LiftLock_LockIot(void);
void LiftLock_UnlockIot(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIFT_LOCK_H__ */
