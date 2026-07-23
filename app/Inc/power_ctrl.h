#ifndef __POWER_CTRL_H
#define __POWER_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * V6 power-rail control (all outputs start LOW after reset via gpio.c):
 *   PE6 CTRL_DCDCboost_EN — DCDC boost enable        (HIGH = on)
 *   PE5 CTRL_LOAD_OUT     — 24V load output enable   (HIGH = on)
 *   PC15 CTRL_CHARGE_NTC  — battery charger NTC enable (HIGH = on)
 *   PC10 FAN_VCC_CTRL     — fan 12V rail enable      (HIGH = on)
 *   PC11 PUMP_CTRL        — water pump enable         (HIGH = on)
 *
 * Startup sequence: PE6 → 200ms → PE5 → 100ms → PC15 → 50ms (each step
 * IWDG-fed). Fan / pump remain off until business commands them on.
 */

void PowerCtrl_StartupSequence(void);

void PowerCtrl_EnableBoost(uint8_t en);       /* PE6 */
void PowerCtrl_EnableLoad(uint8_t en);        /* PE5 */
void PowerCtrl_EnableChargeNtc(uint8_t en);   /* PC15 */
void PowerCtrl_EnableFanVcc(uint8_t en);      /* PC10 */
void PowerCtrl_EnablePump(uint8_t en);        /* PC11; 1s full-power start, then configured duty */
void PowerCtrl_SetPumpDuty(uint8_t duty_pct); /* 0..100%; default 30% */
void PowerCtrl_PumpPwmTick2kHz(void);          /* call only from TIM7 update ISR */

#endif /* __POWER_CTRL_H */
