#ifndef __POWER_CTRL_H
#define __POWER_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * Power rail control (all outputs start LOW after reset via gpio.c):
 *   CHARGE_EN  — PB12: lithium battery charger enable (HIGH = on)
 *   EN_TPS43060— PB13: 12V→24V boost enable           (HIGH = on)
 *   EN_24TO12  — PB14: 24V→12V buck enable            (HIGH = on)
 *   PUMP_EN    — PB15: pump output enable              (HIGH = on)
 *
 * Startup sequence: PB13 → 500ms → PB14 → 200ms → PB12 → 50ms → PB15
 */

void PowerCtrl_StartupSequence(void);

void PowerCtrl_EnableCharger(uint8_t en);   /* PB12 */
void PowerCtrl_EnableBoost(uint8_t en);     /* PB13 */
void PowerCtrl_EnableBuck(uint8_t en);      /* PB14 */
void PowerCtrl_EnablePump(uint8_t en);      /* PB15 */

#endif /* __POWER_CTRL_H */
