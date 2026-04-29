/**
 * @file    app/Src/buzzer.c
 * @brief   Passive buzzer on PB14 / TIM1_CH2N (V6 board).
 *
 * V3 was PA15/TIM2_CH1; V6 reuses PA15 for FAN_PWM, so buzzer moved to
 * PB14 which is TIM1_CH2N (an advanced-timer complementary output).
 * Two extra concerns vs V3:
 *   1. __HAL_TIM_MOE_ENABLE() must be called in Init or the output is muted.
 *   2. Use HAL_TIMEx_PWMN_Start/Stop, not HAL_TIM_PWM_Start.
 *
 * LED status output during chimes (V3 toggled PD13/PD14) removed — the
 * led_rgb module owns the new RGB LEDs (PE2/PE3/PE4) and runs independently.
 * Buzzer only feeds the IWDG inside long tunes.
 */
#include "buzzer.h"

static TIM_HandleTypeDef *s_htim;

void Buzzer_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;
    __HAL_TIM_MOE_ENABLE(s_htim);   /* 高级定时器: BDTR.MOE=1, 否则 CH2N 静音 */
}

static inline void buzzer_set_freq_50pct(uint32_t freq_hz)
{
    if (freq_hz == 0) return;
    uint32_t arr = 1000000UL / freq_hz - 1U;
    __HAL_TIM_SET_AUTORELOAD(s_htim, arr);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_2, arr / 2U);
}

void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0) return;
    buzzer_set_freq_50pct(freq_hz);
    __HAL_TIM_MOE_ENABLE(s_htim);   /* PWMN_Stop 会清 MOE, 每次 Start 前必须重启 */
    HAL_TIMEx_PWMN_Start(s_htim, TIM_CHANNEL_2);
    HAL_Delay(duration_ms);
    HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_2);
}

void Buzzer_Stop(void)
{
    HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_2);
}

void Buzzer_PlayHajimi(void)
{
    /* 祝你生日快乐 — Happy Birthday to You (G大调, ~90BPM, ≈10s)
     * 必须在循环里喂狗 (IWDG 4s 超时). */
    static const uint16_t notes[] = {
        392, 392, 440, 392, 523, 494,
        392, 392, 440, 392, 587, 523,
        392, 392, 784, 659, 523, 494, 440,
        698, 698, 659, 523, 587, 523
    };
    static const uint16_t durs[] = {
        300, 100, 200, 200, 200, 400,
        300, 100, 200, 200, 200, 400,
        300, 100, 200, 200, 200, 200, 400,
        300, 100, 200, 200, 200, 400
    };

    for (uint32_t i = 0; i < (sizeof(notes) / sizeof(notes[0])); i++) {
        IWDG->KR = 0xAAAAU;
        buzzer_set_freq_50pct(notes[i]);
        __HAL_TIM_MOE_ENABLE(s_htim);   /* PWMN_Stop 会清 MOE, 每次 Start 前必须重启 */
        HAL_TIMEx_PWMN_Start(s_htim, TIM_CHANNEL_2);
        HAL_Delay(durs[i]);
        HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_2);
        HAL_Delay(20);
    }
}

void Buzzer_PlayStartup(void)
{
    /* "凌犀创新，欢迎你" — 五声音阶模拟普通话声调轮廓. notes=0 → 静音停顿. */
    static const uint16_t notes[] = { 784, 988, 1047,   0, 1175, 784, 880,   0, 988, 880, 1047, 784 };
    static const uint16_t durs[]  = { 140, 200,  260, 100,  130, 150, 350, 200, 180, 140,  200, 420 };

    for (uint32_t i = 0; i < 12; i++) {
        IWDG->KR = 0xAAAAU;
        if (notes[i]) {
            buzzer_set_freq_50pct(notes[i]);
            HAL_TIMEx_PWMN_Start(s_htim, TIM_CHANNEL_2);
            HAL_Delay(durs[i]);
            HAL_TIMEx_PWMN_Stop(s_htim, TIM_CHANNEL_2);
            HAL_Delay(30);
        } else {
            HAL_Delay(durs[i]);
        }
    }
}
