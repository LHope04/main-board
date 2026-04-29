#ifndef __BUTTON_H
#define __BUTTON_H

#include "stm32f4xx_hal.h"

/*
 * Front-panel button on V6 board:
 *   PC13 BUTTON  — pull-up, active LOW (pressed)
 *   PC14 ACC     — secondary input (similar wiring), reserved for future
 *   PA0  KEYWAKE — wake input, optional
 *
 * This module only handles PC13 short/long press. ACC and KEYWAKE are
 * exposed via raw read helpers for callers that need them.
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
uint8_t   Button_ReadKeywake(void);      /* PA0,  1=active */

#endif /* __BUTTON_H */
