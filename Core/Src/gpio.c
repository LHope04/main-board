/**
 * @file    Core/Src/gpio.c
 * @brief   GPIO configuration for V6 main board (STM32F407VET6).
 *
 * Pin map (authoritative source: 板子/SCH_Schematic6_2026-04-21 + IO 未分配 page).
 * AF pins (TIM/I2C/USART) are configured by their respective MspInit, not here.
 *
 * Layout:
 *   - Outputs default to safe state (LOW for all enables/drivers)
 *   - Inputs: BUTTON / ACC / nFAULT / KEYWAKE
 *   - LEDs: PE2 (R) / PE3 (G) / PE4 (B)
 *   - MCF8329A control: PC12 DROFF / PD0 SPEED_WAKE / PD3 DIR / PD4 BREAK
 *   - Power enables: PE5 LOAD / PE6 BOOST / PC10 FAN_VCC / PC11 PUMP / PC15 CHARGE_NTC
 */
#include "gpio.h"

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();   /* OSC_IN/OUT (PH0/PH1) */

    /* ====== Outputs: default LOW (safe state) ====== */

    /* RGB LED: PE2 R, PE3 G, PE4 B — common-anode wiring, default HIGH=off */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_SET);

    /* Power enables — polarity per board design:
     *   PE6 BOOST       active LOW  → default HIGH = OFF
     *   PE5 LOAD        active HIGH → default LOW  = OFF
     *   PC15 CHARGE_NTC active LOW  → default HIGH = OFF */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_RESET);   /* LOAD off */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_SET);     /* BOOST off (active LOW) */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);    /* CHARGE off (active LOW) */

    /* Fan VCC + Pump: PC10, PC11 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10 | GPIO_PIN_11, GPIO_PIN_RESET);

    /* MCF8329A 主控 I2C 控制模式 (motor RUN 状态默认):
     *   PC12 DRVOFF      推挽输出 LOW  (driver enabled)
     *   PD0  SPEED_WAKE  推挽输出 HIGH (out of sleep)
     *   PD3  DIR         推挽输出 LOW  (CW)
     *   PD4  BREAK       推挽输出 LOW  (RUN, no brake)
     *   PD5  nFAULT      浮空输入 (外部 5.1kΩ 上拉到 DVCC) */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);                /* DRVOFF=0 */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_RESET);    /* DIR=0, BREAK=0 */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);                   /* SPEED_WAKE=1 */

    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    gi.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOE, &gi);

    /* PC10/11/12/15 = FAN_VCC, PUMP, DRVOFF, CHARGE_NTC */
    gi.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOC, &gi);

    /* PD0/PD3/PD4 = SPEED_WAKE, DIR, BREAK */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_3 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOD, &gi);

    /* ====== Inputs ====== */

    /* BUTTON (PC13), ACC (PC14): pull-up, active LOW */
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin   = GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOC, &gi);

    /* MCF8329A nFAULT (PD5): 外部 10kΩ 上拉,内部 NOPULL 避免冲突 */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_NOPULL;
    gi.Pin  = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOD, &gi);

    /* KEYWAKE (PA0/SYS_WKUP): no pull (WKUP hardware controls) */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_NOPULL;
    gi.Pin  = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &gi);

    /* AF pins (TIM1/2/3/4/14, I2C1/2/3, USART1/2/3/6) — configured by their
     * own MspInit / MX init functions. Not initialized here. */
}
