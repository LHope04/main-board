#ifndef __BUTTON_H
#define __BUTTON_H

#include "stm32f4xx_hal.h"

/*
 * Front-panel button on V6 board:
 *   PC13 BUTTON  — pull-up, active LOW (pressed)
 *   PC14 ACC     — secondary input (similar wiring), reserved for future
 *
 * This module handles PC13 short/long press and exposes ACC as a raw input.
 * PA0 is reserved for compressor TIM2_CH1 PWM and is not a key input.
 *
 * Debounce: 20ms. Long-press threshold: 1000ms.
 * State machine driven by polling — call Button_Poll() at ~100Hz from main
 * loop. Returns at most one event per call.
 */

typedef enum {
    BUTTON_EVT_NONE,
    BUTTON_EVT_SHORT,
    BUTTON_EVT_LONG,
} ButtonEvt;

void      Button_Init(void);
ButtonEvt Button_Poll(void);
uint8_t   Button_ReadAcc(void);          /* PC14, 1=active (LOW) */

#endif /* __BUTTON_H */
