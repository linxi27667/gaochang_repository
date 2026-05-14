#ifndef __DRI_DEBUG_H__
#define __DRI_DEBUG_H__

#include "main.h"

/* 模拟编码器：每 50ms 1 个脉冲 */
#define SIM_PULSE_INTERVAL_MS  50

/* 模拟防碰杆高度（仅 COLLISION_ENABLE=0 时有效） */
#define SIM_COLLISION_HEIGHT_MM  500

void Sim_Encoder_Init(void);
void Sim_Encoder_Run(void);
void Sim_Collision_Check(void);

#endif
