#ifndef __FAN_CTRL_H
#define __FAN_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * V6 fan control (was V3 PC8 TIM3_CH3 PWM + PC7 TIM8_CH2 IC):
 *   PWM    — TIM2_CH1, PA15 (AF1), 20kHz, ARR=49 (84MHz / 84 / 50 = 20kHz)
 *   FB IC  — TIM3_CH1, PB4  (AF2), 1MHz tick, ARR=65535 (~65.5ms span)
 *   VCC EN — PowerCtrl_EnableFanVcc(): PC10 OUTPUT_PP
 *
 * RPM formula: rpm = freq_hz * 20 (3 pole pairs)
 * FG valid range: 1~1000 Hz
 *
 * NOTE: MX_TIM2_FANPWM_Init/MX_TIM3_FANIC_Init in main.c configure TIM bases.
 * FanCtrl_Init only starts PWM CH1 + IC CH1.
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
