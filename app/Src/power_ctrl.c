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

#define PUMP_PWM_PERIOD_TICKS 20U   /* TIM7 2kHz / 20 = 100Hz */
#define PUMP_DEFAULT_DUTY_PCT 30U

volatile uint8_t  g_pump_duty_pct = PUMP_DEFAULT_DUTY_PCT;
volatile uint8_t  g_pump_pwm_phase = 0U;
volatile uint8_t  g_pump_output_on = 0U;
volatile uint16_t g_pump_startup_ticks = 0U;

static volatile uint8_t s_pump_enabled = 0U;

static inline void pump_write(uint8_t on)
{
    /* Direct BSRR write keeps the 2kHz ISR short and changes only PC11. */
    GPIOC->BSRR = on ? (uint32_t)GPIO_PIN_11
                     : ((uint32_t)GPIO_PIN_11 << 16U);
    g_pump_output_on = on ? 1U : 0U;
}

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

    /* 风扇/水泵开机默认 OFF, 由业务命令开启 (gpio.c init 已设 PC10/PC11=LOW). */
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
    if (en) {
        g_pump_pwm_phase = 0U;
        g_pump_startup_ticks = 0U; /* no full-speed startup */
        s_pump_enabled = 1U;
        pump_write(g_pump_duty_pct > 0U);
    } else {
        s_pump_enabled = 0U;
        g_pump_startup_ticks = 0U;
        g_pump_pwm_phase = 0U;
        pump_write(0U);
    }
}

void PowerCtrl_SetPumpDuty(uint8_t duty_pct)
{
    if (duty_pct > 100U) duty_pct = 100U;
    g_pump_duty_pct = duty_pct;

    if (duty_pct == 0U) {
        g_pump_pwm_phase = 0U;
        pump_write(0U);
    }
}

void PowerCtrl_PumpPwmTick2kHz(void)
{
    uint8_t duty_pct;
    uint8_t on_ticks;

    if (!s_pump_enabled) return;

    duty_pct = g_pump_duty_pct;
    if (duty_pct == 0U) {
        g_pump_pwm_phase = 0U;
        pump_write(0U);
        return;
    }

    if (duty_pct >= 100U) {
        g_pump_pwm_phase = 0U;
        pump_write(1U);
        return;
    }

    on_ticks = (uint8_t)(((uint16_t)duty_pct * PUMP_PWM_PERIOD_TICKS + 50U) / 100U);
    if (on_ticks == 0U) on_ticks = 1U;

    if (g_pump_pwm_phase == 0U) {
        pump_write(1U);
    } else if (g_pump_pwm_phase == on_ticks) {
        pump_write(0U);
    }

    g_pump_pwm_phase++;
    if (g_pump_pwm_phase >= PUMP_PWM_PERIOD_TICKS) {
        g_pump_pwm_phase = 0U;
    }
}
