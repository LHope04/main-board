#ifndef __COMPRESSOR_CTRL_H
#define __COMPRESSOR_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * V6 compressor control: TI MCF8329A 三相 BLDC sensorless driver via I2C3.
 * Rewrites V3's PMOS+single-PWM model. STAGE 1 keeps the old function names
 * as STUBs so main.c still compiles; real implementation lands in 阶段 6.
 *
 * Hardware signals (set by gpio.c, used by mcf8329a driver):
 *   PA8/PC9   I2C3 SCL/SDA  — register R/W
 *   PC12      DROFF         — hardware enable (LOW=disabled)
 *   PD0       SPEED_WAKE    — wake/sleep
 *   PD3       DIR           — direction (LOW=CW)
 *   PD4       BREAK         — brake (LOW=engaged)
 *   PD5       nFAULT        — input, LOW=fault
 *   PB9       FG (TIM4_CH4) — input capture for speed feedback
 *   PC1       SOX           — ADC1_IN11 current sense (stage 6, 待启用)
 *   PA7       EXT_CLK (TIM14_CH1) — optional external clock (stub)
 *
 * RPM formula unchanged: rpm = freq_hz * 10 (6 pole pairs).
 */

/* === 旧 API (STUB in stage 1, real impl in stage 6) === */
void     CompressorCtrl_Init(TIM_HandleTypeDef *htim_fg);   /* TIM4 */
void     CompressorCtrl_SetDuty(uint8_t percent);           /* legacy: stage 6 maps to SetSpeed */
void     CompressorCtrl_SetDirection(uint8_t dir);          /* PD3 GPIO */
void     CompressorCtrl_SetBrake(uint8_t en);               /* PD4 GPIO: 1=release, 0=engage */
float    CompressorCtrl_GetFreqHz(void);
uint32_t CompressorCtrl_GetRPM(void);

void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim);

#endif /* __COMPRESSOR_CTRL_H */
