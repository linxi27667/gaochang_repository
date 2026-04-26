/*
 * app_sync.h - 同步检查任务声明
 */
#ifndef APP_SYNC_H
#define APP_SYNC_H

void Sync_Task(void *arg);

/* 误差阈值：4 圈（与 PLC 一致） */
#define SYNC_THRESHOLD  4

#endif /* APP_SYNC_H */
