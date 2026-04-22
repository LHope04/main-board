#include "compressor_ctrl.h"

static TIM_HandleTypeDef *s_htim;

static volatile uint32_t s_ic_val1     = 0;
static volatile uint32_t s_ic_val2     = 0;
static volatile uint32_t s_ic_overflow = 0;
static volatile uint8_t  s_ic_state    = 0;   /* 0=waiting, 1=first edge captured */
static volatile uint32_t s_freq_hz     = 0;
static uint8_t           s_pwm_started = 0;

void CompressorCtrl_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;

    HAL_TIM_IC_Start_IT(s_htim, TIM_CHANNEL_1);
    __HAL_TIM_ENABLE_IT(s_htim, TIM_IT_UPDATE);
}

void CompressorCtrl_SetDuty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_htim);
    /* TIM3_CH2 uses PMOS (TIM_OCPOLARITY_LOW): CCR = percent * (ARR+1) / 100 */
    uint32_t ccr = (arr + 1U) * percent / 100U;
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, ccr);
    if (!s_pwm_started) {
        HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_2);
        s_pwm_started = 1;
    }
}

void CompressorCtrl_SetDirection(uint8_t dir)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void CompressorCtrl_SetBrake(uint8_t en)
{
    /* en=1: PD7=HIGH → release brake; en=0: PD7=LOW → engage brake */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

float CompressorCtrl_GetFreqHz(void)
{
    return (float)s_freq_hz;
}

uint32_t CompressorCtrl_GetRPM(void)
{
    return s_freq_hz * 10UL;   /* 6 pole pairs: RPM = Hz * 60/6 */
}

void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3 || htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1) return;

    if (s_ic_state == 0) {
        s_ic_val1    = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        s_ic_overflow = 0;
        s_ic_state   = 1;
    } else {
        s_ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        uint32_t ticks;
        /* TIM3 ARR=49, one period = 50 ticks */
        if (s_ic_val2 >= s_ic_val1) {
            ticks = s_ic_overflow * 50U + (s_ic_val2 - s_ic_val1);
        } else {
            ticks = (s_ic_overflow + 1U) * 50U - (s_ic_val1 - s_ic_val2);
        }
        if (ticks > 0U) {
            s_freq_hz = 1000000UL / ticks;
        }
        s_ic_state = 0;
    }
}

void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3) return;

    if (s_ic_state == 1) {
        s_ic_overflow++;
        if (s_ic_overflow > 1000U) {   /* ~50ms timeout: SC_COUNT absent */
            s_freq_hz  = 0;
            s_ic_state = 0;
        }
    }
}
