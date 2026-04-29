/**
 * @file    stm32f4xx_hal_msp.c
 * @brief   MSP init for V6 board peripherals.
 *
 * HAL_TIM_PWM_Init / HAL_TIM_IC_Init call into these MspInit hooks during
 * peripheral bring-up. The default weak implementations do nothing, so if
 * we don't supply our own the AF pins stay as plain GPIOs and the timer
 * outputs never reach the pads.
 *
 * Pins handled here:
 *   TIM1_CH2N  PB14  AF1   buzzer (advanced timer)
 *   TIM2_CH1   PA15  AF1   fan PWM
 *   TIM3_CH1   PB4   AF2   fan FG input capture
 *   TIM4_CH4   PB9   AF2   compressor FG input capture
 */
#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_AF_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;

    if (htim->Instance == TIM1) {
        /* BEEP_CTRL: PB14 / TIM1_CH2N / AF1 */
        __HAL_RCC_TIM1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_14;
        gi.Alternate = GPIO_AF1_TIM1;
        HAL_GPIO_Init(GPIOB, &gi);
    }
    else if (htim->Instance == TIM2) {
        /* FAN_PWM_CTRL: PA15 / TIM2_CH1 / AF1 */
        __HAL_RCC_TIM2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_15;
        gi.Alternate = GPIO_AF1_TIM2;
        HAL_GPIO_Init(GPIOA, &gi);
    }
}

void HAL_TIM_IC_MspInit(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_AF_PP;
    gi.Pull  = GPIO_PULLUP;          /* FG/SC are open-drain on the driver IC */
    gi.Speed = GPIO_SPEED_FREQ_HIGH;

    if (htim->Instance == TIM3) {
        /* FAN_FB_OUT: PB4 / TIM3_CH1 / AF2 */
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_4;
        gi.Alternate = GPIO_AF2_TIM3;
        HAL_GPIO_Init(GPIOB, &gi);
    }
    else if (htim->Instance == TIM4) {
        /* MCF8329A FG: PB9 / TIM4_CH4 / AF2 */
        __HAL_RCC_TIM4_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_9;
        gi.Alternate = GPIO_AF2_TIM4;
        HAL_GPIO_Init(GPIOB, &gi);
    }
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    /* Cover the case where someone calls HAL_TIM_Base_Init separately. */
    if (htim->Instance == TIM1) __HAL_RCC_TIM1_CLK_ENABLE();
    else if (htim->Instance == TIM2) __HAL_RCC_TIM2_CLK_ENABLE();
    else if (htim->Instance == TIM3) __HAL_RCC_TIM3_CLK_ENABLE();
    else if (htim->Instance == TIM4) __HAL_RCC_TIM4_CLK_ENABLE();
}
