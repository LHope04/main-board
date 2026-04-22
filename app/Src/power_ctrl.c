#include "power_ctrl.h"

void PowerCtrl_StartupSequence(void)
{
    HAL_Delay(100);

    /* step1: EN_TPS43060 (PB13) 12->24V */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(500);

    /* step2: EN_24TO12 (PB14) 24->12V */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
    HAL_Delay(200);

    /* step3: CHARGE_EN (PB12) disabled */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_Delay(50);

    /* step4: PUMP_EN (PB15) — 开机保持关闭，等ESP32命令开启 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
    HAL_Delay(50);
}

void PowerCtrl_EnableCharger(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PowerCtrl_EnableBoost(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PowerCtrl_EnableBuck(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PowerCtrl_EnablePump(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
