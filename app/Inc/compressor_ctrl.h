#ifndef __COMPRESSOR_CTRL_H
#define __COMPRESSOR_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * V7 compressor interface:
 *   PA15 / TIM2_CH1 AF1 — 5kHz PWM, active HIGH
 *   PC9  / NET14        — direction: HIGH=forward, LOW=reverse
 *   PA8  / NET15        — stop: LOW=stop, HIGH=run
 *
 * Safe state is PWM=0 and PA8 LOW. The bootloader and App GPIO init both
 * assert this state before normal application control begins.
 */

extern volatile uint8_t g_compressor_pwm_duty_pct;
extern volatile uint8_t g_compressor_reverse;
extern volatile uint8_t g_compressor_stop_asserted;
extern volatile uint8_t g_compressor_ramp_active;
extern volatile uint8_t g_compressor_ramp_target_pct;
extern volatile uint32_t g_compressor_ramp_elapsed_ms;

#define COMPRESSOR_SOFT_START_MS 5000U
#define COMPRESSOR_SOFT_START_ENABLED 0U

void CompressorCtrl_Init(TIM_HandleTypeDef *htim_pwm); /* TIM2_CH1 / PA15 */
void CompressorCtrl_SetDuty(uint8_t percent);
void CompressorCtrl_SetDirection(uint8_t reverse);
void CompressorCtrl_SetStop(uint8_t stop);
void CompressorCtrl_Start(uint8_t percent);
void CompressorCtrl_Stop(void);
void CompressorCtrl_Task(uint32_t now_ms);

/* Compatibility API; V7 maps brake asserted to STOP asserted. */
void CompressorCtrl_SetBrake(uint8_t engage);

/* V7 currently has no speed feedback input assigned. */
float    CompressorCtrl_GetFreqHz(void);
uint32_t CompressorCtrl_GetRPM(void);
void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim);
void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim);

#endif /* __COMPRESSOR_CTRL_H */
