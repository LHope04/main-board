#ifndef __FAN_CTRL_H
#define __FAN_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * Fan control:
 *   PWM  — TIM3_CH3, PC8 (AF2), 20kHz, ARR=49
 *   Power— FAN_VCC_CTRL PC6 (OUTPUT_PP, HIGH=on)
 *   FG   — TIM8_CH2, PC7 (AF3, GPIO_PULLUP), input capture
 *
 * RPM formula: fan_rpm = freq_hz * 20  (3 pole pairs → 60/3 = 20)
 * FG valid range: 1~1000 Hz (rejects 20kHz PWM crosstalk)
 */

void     FanCtrl_Init(TIM_HandleTypeDef *htim_pwm,   /* TIM3 */
                      TIM_HandleTypeDef *htim_ic);    /* TIM8 */
void     FanCtrl_Enable(uint8_t en);                  /* PC6: 0=off, 1=on */
void     FanCtrl_SetDuty(uint8_t percent);            /* 0~100 % */
float    FanCtrl_GetFreqHz(void);
uint32_t FanCtrl_GetRPM(void);

/* Call from HAL_TIM_IC_CaptureCallback / HAL_TIM_PeriodElapsedCallback in main.c */
void FanCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void FanCtrl_OverflowCallback(TIM_HandleTypeDef *htim);

#endif /* __FAN_CTRL_H */
