#include "fan_ctrl.h"

static TIM_HandleTypeDef *s_htim_pwm;
static TIM_HandleTypeDef *s_htim_ic;

static volatile uint32_t s_ic_val1     = 0;
static volatile uint32_t s_ic_val2     = 0;
static volatile uint32_t s_ic_overflow = 0;
static volatile uint8_t  s_ic_state    = 0;   /* 0=waiting, 1=first edge captured */
static volatile uint32_t s_freq_hz     = 0;
static uint8_t           s_pwm_started = 0;

void FanCtrl_Init(TIM_HandleTypeDef *htim_pwm, TIM_HandleTypeDef *htim_ic)
{
    s_htim_pwm = htim_pwm;
    s_htim_ic  = htim_ic;

    HAL_TIM_IC_Start_IT(s_htim_ic, TIM_CHANNEL_2);
    __HAL_TIM_ENABLE_IT(s_htim_ic, TIM_IT_UPDATE);
}

void FanCtrl_Enable(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void FanCtrl_SetDuty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_htim_pwm);
    uint32_t ccr = (arr + 1U) * percent / 100U;
    __HAL_TIM_SET_COMPARE(s_htim_pwm, TIM_CHANNEL_3, ccr);
    if (!s_pwm_started) {
        HAL_TIM_PWM_Start(s_htim_pwm, TIM_CHANNEL_3);
        s_pwm_started = 1;
    }
}

float FanCtrl_GetFreqHz(void)
{
    return (float)s_freq_hz;
}

uint32_t FanCtrl_GetRPM(void)
{
    return s_freq_hz * 20UL;   /* 3 pole pairs: RPM = Hz * 60/3 */
}

void FanCtrl_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM8 || htim->Channel != HAL_TIM_ACTIVE_CHANNEL_2) return;

    if (s_ic_state == 0) {
        s_ic_val1    = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
        s_ic_overflow = 0;
        s_ic_state   = 1;
    } else {
        s_ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
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
    if (htim->Instance != TIM8) return;

    if (s_ic_state == 1) {
        s_ic_overflow++;
        if (s_ic_overflow > 100U) {   /* ~6.5s timeout: FG absent */
            s_freq_hz  = 0;
            s_ic_state = 0;
        }
    }
}
