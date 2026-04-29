#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f4xx_hal.h"

/*
 * V6: passive buzzer on PB14, TIM1_CH2N AF1 (advanced timer, complementary output).
 * Timer config: PSC=167 → 1MHz tick (TIM1 on APB2×2=168MHz).
 * ARR = (1MHz / freq_hz) - 1.
 *
 * 高级定时器陷阱: __HAL_TIM_MOE_ENABLE() 必须在 Buzzer_Init 里调, 否则 CH2N
 * 互补输出无信号. 启动用 HAL_TIMEx_PWMN_Start (不是 HAL_TIM_PWM_Start).
 */

void Buzzer_Init(TIM_HandleTypeDef *htim);
void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms);
void Buzzer_Stop(void);
void Buzzer_PlayStartup(void);
void Buzzer_PlayHajimi(void);

#endif /* __BUZZER_H */
