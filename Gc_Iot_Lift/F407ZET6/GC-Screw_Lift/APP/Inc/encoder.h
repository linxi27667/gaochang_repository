#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

void     Encoder_Init(void);
int32_t  Encoder_Get_Count(uint8_t column_index);
void     Encoder_Reset_Count(uint8_t column_index);
void     Encoder_Capture_ISR(uint8_t channel);

#endif
