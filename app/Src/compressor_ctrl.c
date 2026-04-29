/**
 * @file    app/Src/compressor_ctrl.c
 * @brief   Compressor control STUB (stage 1) — real impl in stage 6 with
 *          mcf8329a driver via I2C3.
 *
 * Stage 1: keeps V3's CompressorCtrl_* API surface so main.c compiles.
 * Internally:
 *   - SetDuty(percent) is a no-op (legacy duty-style command, stage 6 maps
 *     to MCF8329A SetSpeed register write).
 *   - SetDirection(dir) writes PD3 GPIO directly.
 *   - SetBrake(en) writes PD4 GPIO directly.
 *   - FG input capture on TIM4_CH4 — algorithm copied from V3 fan_ctrl.
 */
#include "compressor_ctrl.h"

static TIM_HandleTypeDef *s_htim_fg;   /* TIM4 */

static volatile uint32_t s_ic_val1     = 0;
static volatile uint32_t s_ic_val2     = 0;
static volatile uint32_t s_ic_overflow = 0;
static volatile uint8_t  s_ic_state    = 0;
static volatile uint32_t s_freq_hz     = 0;

void CompressorCtrl_Init(TIM_HandleTypeDef *htim_fg)
{
    s_htim_fg = htim_fg;
    HAL_TIM_IC_Start_IT(s_htim_fg, TIM_CHANNEL_4);
    __HAL_TIM_ENABLE_IT(s_htim_fg, TIM_IT_UPDATE);
}

void CompressorCtrl_SetDuty(uint8_t percent)
{
    /* TODO(stage 6): map duty → MCF8329A SPEED register via I2C3.
     * Stage 1: no-op. */
    (void)percent;
}

void CompressorCtrl_SetDirection(uint8_t dir)
{
    /* PD3 DIR: 0=CW (default), 1=reverse */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void CompressorCtrl_SetBrake(uint8_t en)
{
    /* PD4 BREAK: en=1 → PD4=HIGH → release brake; en=0 → engage */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

float    CompressorCtrl_GetFreqHz(void) { return (float)s_freq_hz; }
uint32_t CompressorCtrl_GetRPM(void)    { return s_freq_hz * 10UL; }

void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM4 || htim->Channel != HAL_TIM_ACTIVE_CHANNEL_4) return;

    if (s_ic_state == 0) {
        s_ic_val1     = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_4);
        s_ic_overflow = 0;
        s_ic_state    = 1;
    } else {
        s_ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_4);
        uint32_t ticks;
        if (s_ic_val2 >= s_ic_val1) {
            ticks = s_ic_overflow * 65536U + (s_ic_val2 - s_ic_val1);
        } else {
            ticks = (s_ic_overflow + 1U) * 65536U - (s_ic_val1 - s_ic_val2);
        }
        if (ticks > 0U) {
            uint32_t f = 1000000UL / ticks;
            s_freq_hz = (f >= 1U && f <= 2000U) ? f : 0U;
        }
        s_ic_state = 0;
    }
}

void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM4) return;

    if (s_ic_state == 1) {
        s_ic_overflow++;
        if (s_ic_overflow > 100U) {
            s_freq_hz  = 0;
            s_ic_state = 0;
        }
    }
}
