#ifndef __FAN_CTRL_H
#define __FAN_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * Legacy V6 fan control (not initialized on current hardware):
 *   Former PWM — TIM2_CH1 / PA15. TIM2_CH1 is now compressor PWM on PA0.
 *   FB IC  — TIM3_CH1, PB4  (AF2), 1MHz tick, ARR=65535 (~65.5ms span)
 *   VCC EN — PowerCtrl_EnableFanVcc(): PC10 OUTPUT_PP
 *
 * RPM formula: rpm = freq_hz * 20 (3 pole pairs)
 * FG valid range: 1~1000 Hz
 *
 * Current fan speed PWM is unused; fan power is controlled by PC10 only.
 */

void     FanCtrl_Init(TIM_HandleTypeDef *htim_pwm,   /* TIM2 */
                      TIM_HandleTypeDef *htim_ic);    /* TIM3 */
void     FanCtrl_Enable(uint8_t en);                  /* delegates to PowerCtrl_EnableFanVcc */
void     FanCtrl_SetDuty(uint8_t percent);            /* 0~100 % */
float    FanCtrl_GetFreqHz(void);
uint32_t FanCtrl_GetRPM(void);

void FanCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void FanCtrl_OverflowCallback(TIM_HandleTypeDef *htim);

#endif /* __FAN_CTRL_H */
