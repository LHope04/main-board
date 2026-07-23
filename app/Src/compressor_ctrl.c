/**
 * @file    app/Src/compressor_ctrl.c
 * @brief   V7 compressor control: PA15 PWM + PC9 DIR + PA8 active-low STOP.
 */
#include "compressor_ctrl.h"

volatile uint8_t g_compressor_pwm_duty_pct = 0U;
volatile uint8_t g_compressor_reverse = 1U;
volatile uint8_t g_compressor_stop_asserted = 1U;
volatile uint8_t g_compressor_ramp_active = 0U;
volatile uint8_t g_compressor_ramp_target_pct = 0U;
volatile uint32_t g_compressor_ramp_elapsed_ms = 0U;

static TIM_HandleTypeDef *s_htim_pwm;
static uint32_t s_ramp_start_ms;

static void apply_pwm_duty(void)
{
    uint32_t period_ticks;
    uint32_t compare;

    if (s_htim_pwm == NULL) return;

    period_ticks = __HAL_TIM_GET_AUTORELOAD(s_htim_pwm) + 1U;
    compare = ((uint32_t)g_compressor_pwm_duty_pct * period_ticks) / 100U;
    __HAL_TIM_SET_COMPARE(s_htim_pwm, TIM_CHANNEL_1, compare);
}

void CompressorCtrl_Init(TIM_HandleTypeDef *htim_pwm)
{
    s_htim_pwm = htim_pwm;

    /* Safe state before the timer output is enabled. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET); /* STOP asserted */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET); /* reverse */
    __HAL_TIM_SET_COMPARE(s_htim_pwm, TIM_CHANNEL_1, 0U);
    HAL_TIM_PWM_Start(s_htim_pwm, TIM_CHANNEL_1);

    g_compressor_pwm_duty_pct = 0U;
    g_compressor_reverse = 1U;
    g_compressor_stop_asserted = 1U;
    g_compressor_ramp_active = 0U;
    g_compressor_ramp_target_pct = 0U;
    g_compressor_ramp_elapsed_ms = 0U;
}

void CompressorCtrl_SetDuty(uint8_t percent)
{
    if (percent > 100U) percent = 100U;
    g_compressor_ramp_active = 0U;
    g_compressor_ramp_target_pct = percent;
    g_compressor_ramp_elapsed_ms = 0U;
    g_compressor_pwm_duty_pct = percent;

    if (!g_compressor_stop_asserted) {
        apply_pwm_duty();
    }
}

void CompressorCtrl_SetDirection(uint8_t reverse)
{
    g_compressor_reverse = reverse ? 1U : 0U;
    /* User-confirmed polarity: LOW=reverse, HIGH=forward. */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9,
                      reverse ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void CompressorCtrl_SetStop(uint8_t stop)
{
    if (stop) {
        /* Remove PWM before asserting active-low STOP. */
        if (s_htim_pwm != NULL) {
            __HAL_TIM_SET_COMPARE(s_htim_pwm, TIM_CHANNEL_1, 0U);
        }
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
        g_compressor_pwm_duty_pct = 0U;
        g_compressor_stop_asserted = 1U;
        g_compressor_ramp_active = 0U;
        g_compressor_ramp_elapsed_ms = 0U;
    } else {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
        g_compressor_stop_asserted = 0U;
        apply_pwm_duty();
    }
}

void CompressorCtrl_Start(uint8_t percent)
{
    if (percent > 100U) percent = 100U;

    CompressorCtrl_SetStop(1U);
    g_compressor_ramp_target_pct = percent;
    g_compressor_ramp_elapsed_ms = 0U;

#if COMPRESSOR_SOFT_START_ENABLED
    g_compressor_pwm_duty_pct = 0U;
    s_ramp_start_ms = HAL_GetTick();
    g_compressor_ramp_active = (percent > 0U) ? 1U : 0U;
#else
    g_compressor_pwm_duty_pct = percent;
    g_compressor_ramp_active = 0U;
#endif

    if (percent == 0U) {
        return;
    }

    CompressorCtrl_SetStop(0U);
}

void CompressorCtrl_Stop(void)
{
    CompressorCtrl_SetStop(1U);
}

void CompressorCtrl_Task(uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint8_t duty_pct;

    if (!g_compressor_ramp_active || g_compressor_stop_asserted) {
        return;
    }

    elapsed_ms = (uint32_t)(now_ms - s_ramp_start_ms);
    if (elapsed_ms >= COMPRESSOR_SOFT_START_MS) {
        g_compressor_ramp_elapsed_ms = COMPRESSOR_SOFT_START_MS;
        g_compressor_pwm_duty_pct = g_compressor_ramp_target_pct;
        g_compressor_ramp_active = 0U;
        apply_pwm_duty();
        return;
    }

    g_compressor_ramp_elapsed_ms = elapsed_ms;
    duty_pct = (uint8_t)(((uint32_t)g_compressor_ramp_target_pct * elapsed_ms) /
                         COMPRESSOR_SOFT_START_MS);
    if (duty_pct != g_compressor_pwm_duty_pct) {
        g_compressor_pwm_duty_pct = duty_pct;
        apply_pwm_duty();
    }
}

void CompressorCtrl_SetBrake(uint8_t engage)
{
    CompressorCtrl_SetStop(engage);
}

float CompressorCtrl_GetFreqHz(void) { return 0.0f; }
uint32_t CompressorCtrl_GetRPM(void) { return 0U; }
void CompressorCtrl_CaptureCallback(TIM_HandleTypeDef *htim) { (void)htim; }
void CompressorCtrl_OverflowCallback(TIM_HandleTypeDef *htim) { (void)htim; }
