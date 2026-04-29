#ifndef __LED_RGB_H
#define __LED_RGB_H

#include "stm32f4xx_hal.h"

/*
 * RGB LED on V6 board:
 *   PE2  RED
 *   PE3  GREEN
 *   PE4  BLUE
 *
 * GPIO outputs initialised by gpio.c (default LOW = OFF, assuming common
 * cathode wiring; flip logic if hardware turns out to be common anode).
 *
 * API style: simple level setter + a Tick() helper that drives a built-in
 * mode (solid / cycle / blink) so main loop only needs one call per pass.
 */

typedef enum {
    LED_MODE_OFF,
    LED_MODE_SOLID,        /* 持续显示 last set 颜色 */
    LED_MODE_CYCLE_RGB,    /* 红→绿→蓝→红, 1Hz */
    LED_MODE_BLINK_RED,
    LED_MODE_BLINK_GREEN,
    LED_MODE_BLINK_BLUE,
} LedMode;

void    LedRgb_Init(void);
void    LedRgb_Set(uint8_t r, uint8_t g, uint8_t b);   /* 0/1 each */
void    LedRgb_SetMode(LedMode m);
LedMode LedRgb_GetMode(void);
void    LedRgb_NextSolidColor(void);                   /* 短按: 切下一个颜色 */
void    LedRgb_Tick(uint32_t now_ms);                  /* 主循环调; 内部按 mode 翻转 */

#endif /* __LED_RGB_H */
