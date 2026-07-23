/**
 * @file    app/Src/fan_ctrl.c
 * @brief   Legacy fan PWM + FG input capture. Current fan uses PC10 power only;
 *          TIM2_CH1 / PA0 belongs to compressor PWM.
 *
 * IC capture/overflow algorithm unchanged from V3 — only the timer/channel
 * bindings move. ARR for TIM3 IC is now 65535 (was 65535 for TIM8 too on V3),
 * so the (overflow * 65536) accumulator math carries over.
 */
#include "fan_ctrl.h"
#include "power_ctrl.h"

static TIM_HandleTypeDef *s_htim_pwm;
static TIM_HandleTypeDef *s_htim_ic;

static volatile uint32_t s_ic_val1     = 0;
static volatile uint32_t s_ic_val2     = 0;
static volatile uint32_t s_ic_overflow = 0;
static volatile uint8_t  s_ic_state    = 0;
static volatile uint32_t s_freq_hz     = 0;
static uint8_t           s_pwm_started = 0;

void FanCtrl_Init(TIM_HandleTypeDef *htim_pwm, TIM_HandleTypeDef *htim_ic)
{
    s_htim_pwm = htim_pwm;
    s_htim_ic  = htim_ic;

    HAL_TIM_IC_Start_IT(s_htim_ic, TIM_CHANNEL_1);
    __HAL_TIM_ENABLE_IT(s_htim_ic, TIM_IT_UPDATE);
}

void FanCtrl_Enable(uint8_t en)
{
    PowerCtrl_EnableFanVcc(en);   /* PC10 GPIO 输出, 由 power_ctrl 管 */
}

void FanCtrl_SetDuty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_htim_pwm);
    uint32_t ccr = (arr + 1U) * percent / 100U;
    __HAL_TIM_SET_COMPARE(s_htim_pwm, TIM_CHANNEL_1, ccr);
    if (!s_pwm_started) {
        HAL_TIM_PWM_Start(s_htim_pwm, TIM_CHANNEL_1);
        s_pwm_started = 1;
    }
}

float FanCtrl_GetFreqHz(void) { return (float)s_freq_hz; }
uint32_t FanCtrl_GetRPM(void) { return s_freq_hz * 20UL; }

void FanCtrl_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3 || htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1) return;

    if (s_ic_state == 0) {
        s_ic_val1     = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        s_ic_overflow = 0;
        s_ic_state    = 1;
    } else {
        s_ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        uint32_t ticks;
        if (s_ic_val2 >= s_ic_val1) {
            ticks = s_ic_overflow * 65536U + (s_ic_val2 - s_ic_val1);
        } else {
            ticks = (s_ic_overflow + 1U) * 65536U - (s_ic_val1 - s_ic_val2);
        }
        if (ticks > 0U) {
            uint32_t f = 1000000UL / ticks;
            s_freq_hz = (f >= 1U && f <= 1000U) ? f : 0U;
        }
        s_ic_state = 0;
    }
}

void FanCtrl_OverflowCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3) return;

    if (s_ic_state == 1) {
        s_ic_overflow++;
        if (s_ic_overflow > 100U) {
            s_freq_hz  = 0;
            s_ic_state = 0;
        }
    }
}
