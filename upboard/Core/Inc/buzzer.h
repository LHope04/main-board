#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f4xx_hal.h"

/*
 * Passive buzzer on PA15, TIM2_CH1 AF1.
 * Timer config: PSC=83 → 1MHz tick. ARR = (1MHz / freq_hz) - 1.
 * Startup chime: "凌犀创新，欢迎你" — 五声音阶模拟普通话声调轮廓
 */

void Buzzer_Init(TIM_HandleTypeDef *htim);
void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms);
void Buzzer_Stop(void);
void Buzzer_PlayStartup(void);
void Buzzer_PlayHajimi(void);

#endif /* __BUZZER_H */
