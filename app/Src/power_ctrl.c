/**
 * @file    app/Src/power_ctrl.c
 * @brief   Power-rail sequencing for V6 board.
 *
 * Polarity (per board design):
 *   PE6 BOOST       active LOW
 *   PE5 LOAD        active HIGH
 *   PC15 CHARGE_NTC active LOW
 *   PC10 FAN_VCC    active HIGH
 *   PC11 PUMP       active HIGH
 *
 * Startup sequence: ~350ms total. Each delay step refreshes IWDG (~4s).
 */
#include "power_ctrl.h"

void PowerCtrl_StartupSequence(void)
{
    HAL_Delay(100);
    IWDG->KR = 0xAAAAU;

    /* step1: DCDC boost enable (PE6, active LOW) */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_Delay(200);
    IWDG->KR = 0xAAAAU;

    /* step2: 24V load output enable (PE5, active HIGH) */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_Delay(100);
    IWDG->KR = 0xAAAAU;

    /* step3: charger NTC enable (PC15, active LOW) */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_RESET);
    HAL_Delay(50);
    IWDG->KR = 0xAAAAU;

    /* fan / pump 默认 OFF, 由业务命令开启 */
}

void PowerCtrl_EnableBoost(uint8_t en)
{
    /* active LOW: en=1 → LOW */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, en ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void PowerCtrl_EnableLoad(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PowerCtrl_EnableChargeNtc(uint8_t en)
{
    /* active LOW: en=1 → LOW */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, en ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void PowerCtrl_EnableFanVcc(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PowerCtrl_EnablePump(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
