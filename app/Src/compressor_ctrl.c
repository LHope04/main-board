/**
 * @file    app/Src/compressor_ctrl.c
 * @brief   Compressor control — wired to MCF8329A via g_mcf_spin_duty
 *          (which the main-loop 200ms tick re-sends as DIGITAL_SPEED_CTRL).
 *
 * - SetDuty(percent 0~100) → g_mcf_spin_duty 0~0x7FFF, takes effect next tick (<200ms).
 *   ⚠ 实测当前压缩机 (Motor Studio MPET 后) 静态起转需要 ≥75%, 稳定运行后可下调到 ~50%.
 *   ESP gear-cmd 映射在 main.c gear handler 里, 建议保留 75-100% 安全区间.
 * - SetDirection(dir) writes PD3 GPIO. ⚠ Motor Studio 把 EEPROM PERI_CONFIG1
 *   DIR_INPUT override 后, PD3 实测无效, 方向由 EEPROM 写定.
 * - SetBrake(engage): PD4=HIGH 触发 MCF8329A 内置 brake (低端 FET 全开短接电机).
 * - FG input capture on TIM4_CH4 — 当前未启用(CompressorCtrl_Init 未在 main.c 调用).
 *   实时转速反馈走 MCF8329A 内部 FG_SPEED_FDBK 寄存器 (g_mcf_fg_speed) via I2C.
 */
#include "compressor_ctrl.h"

extern volatile uint16_t g_mcf_spin_duty;

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
    if (percent > 100U) percent = 100U;
    /* 0~100 → 0~0x7FFF DIGITAL_SPEED_CTRL */
    g_mcf_spin_duty = (uint16_t)(((uint32_t)percent * 0x7FFFU) / 100U);
}

void CompressorCtrl_SetDirection(uint8_t dir)
{
    /* PD3 DIR pin: 0=CW, 1=reverse.
     * ⚠ 若 EEPROM PERI_CONFIG1 DIR_INPUT 设为 override 模式, 此引脚被屏蔽 (实测). */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void CompressorCtrl_SetBrake(uint8_t engage)
{
    /* PD4 BREAK: engage=1 → PD4=HIGH = MCF8329A 内置 brake (低端 FET 全开短接电机).
     *            engage=0 → PD4=LOW  = 释放 (电机自由转动 / 正常运行). */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, engage ? GPIO_PIN_SET : GPIO_PIN_RESET);
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
