#include "buzzer.h"

static TIM_HandleTypeDef *s_htim;

void Buzzer_Init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;
}

void Buzzer_PlayTone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0) return;
    uint32_t arr = 1000000UL / freq_hz - 1U;
    __HAL_TIM_SET_AUTORELOAD(s_htim, arr);
    __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, arr / 2U);
    HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
    HAL_Delay(duration_ms);
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
}

void Buzzer_Stop(void)
{
    HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
}

void Buzzer_PlayHajimi(void)
{
    /* 祝你生日快乐 — Happy Birthday to You
     * G大调: G4=392 A4=440 B4=494 C5=523 D5=587 E5=659 F5=698 G5=784
     *
     * 节拍 ~90BPM: 附点四分=500ms  八分=165ms  四分=330ms  二分=660ms
     *
     * 祝  你  生  日  快  乐
     * 祝  你  生  日  快  乐
     * 祝  你  生  日  亲爱的…
     * 祝  你  生  日  快  乐
     */
    static const uint16_t notes[] = {
        392, 392, 440, 392, 523, 494,   /* 祝你生日快乐 */
        392, 392, 440, 392, 587, 523,   /* 祝你生日快乐 */
        392, 392, 784, 659, 523, 494, 440,  /* 祝你生日亲爱的 */
        698, 698, 659, 523, 587, 523    /* 祝你生日快乐 */
    };
    static const uint16_t durs[] = {
        300, 100, 200, 200, 200, 400,
        300, 100, 200, 200, 200, 400,
        300, 100, 200, 200, 200, 200, 400,
        300, 100, 200, 200, 200, 400
    };

    /* 演奏一遍 ≈ 10s，LED1(PD13)↔LED2(PD14) 随每拍交替 */
    for (uint32_t i = 0; i < 25; i++) {
        IWDG->KR = 0xAAAAU;   /* 曲子 10s > IWDG 4s，必须在循环里喂狗 */
        if (i & 1U) {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_RESET);
        }
        uint32_t arr = 1000000UL / notes[i] - 1U;
        __HAL_TIM_SET_AUTORELOAD(s_htim, arr);
        __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, arr / 2U);
        HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
        HAL_Delay(durs[i]);
        HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);
        HAL_Delay(20);
    }
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);
}

void Buzzer_PlayStartup(void)
{
    /* "凌犀创新，欢迎你" — 用五声音阶模拟普通话声调轮廓
     *
     * 凌(Líng 2声↗): G5(784)→B5(988)  上行
     * 犀(Xī   1声→): C6(1047)          平高
     * [短停顿]
     * 创(Chuàng 4声↘): D6(1175)→G5(784) 下行
     * 新(Xīn  1声→): A5(880)            平高收句
     * [句间停顿]
     * 欢(Huān 1声→): B5(988)            平高
     * 迎(Yíng 2声↗): A5(880)→C6(1047)  上行
     * 你(Nǐ   3声↙): G5(784)            低落收尾
     *
     * notes=0 表示静音停顿
     */
    static const uint16_t notes[] = { 784, 988, 1047,   0, 1175, 784, 880,   0, 988, 880, 1047, 784 };
    static const uint16_t durs[]  = { 140, 200,  260, 100,  130, 150, 350, 200, 180, 140,  200, 420 };

    for (uint32_t i = 0; i < 12; i++) {
        IWDG->KR = 0xAAAAU;
        if (notes[i]) {
            uint32_t arr = 1000000UL / notes[i] - 1U;
            __HAL_TIM_SET_AUTORELOAD(s_htim, arr);
            __HAL_TIM_SET_COMPARE(s_htim, TIM_CHANNEL_1, arr / 2U);
            HAL_TIM_PWM_Start(s_htim, TIM_CHANNEL_1);
            HAL_Delay(durs[i]);
            HAL_TIM_PWM_Stop(s_htim, TIM_CHANNEL_1);
            HAL_Delay(30);   /* 音符间隙 */
        } else {
            HAL_Delay(durs[i]);
        }
    }
}
