#ifndef __COMPRESSOR_CTRL_H
#define __COMPRESSOR_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * Compressor (YSJ) control — shares TIM3 with fan_ctrl:
 *   PWM    — TIM3_CH2, PB5 (AF2), PMOS inverted (TIM_OCPOLARITY_LOW), 20kHz, ARR=49
 *   DIR    — PB3 (OUTPUT_PP): direction control
 *   BRAKE  — PD7 (OUTPUT_PP): HIGH=release brake, LOW=engage brake
 *   SC_COUNT— TIM3_CH1, PB4 (AF2), input capture for speed feedback
 *
 * RPM formula: sc_rpm = freq_hz * 10  (6 pole pairs → 60/6 = 10)
 *
 * NOTE: MX_TIM3_Init() in main.c initialises TIM3 base + all channels.
 *       CompressorCtrl_Init() only starts IC — it does NOT re-init TIM3.
 */

void     CompressorCtrl_Init(TIM_HandleTypeDef *htim);   /* TIM3 */
void     CompressorCtrl_SetDuty(uint8_t percent);        /* 0~100 %, PMOS-corrected */
void     CompressorCtrl_SetDirection(uint8_t dir);       /* PB3: 0=default, 1=reverse */
void     CompressorCtrl_SetBrake(uint8_t en);            /* PD7: 1=release, 0=engage  */
float    CompressorCtrl_GetFreqHz(void);
uint32_t CompressorCtrl_GetRPM(void);

/* Call from HAL_TIM_IC_CaptureCallback / HAL_TIM_PeriodElapsedCallback in main.c */
void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim);

#endif /* __COMPRESSOR_CTRL_H */
