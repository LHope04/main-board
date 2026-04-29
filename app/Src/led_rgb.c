/**
 * @file    app/Src/led_rgb.c
 * @brief   RGB LED on PE2 (R) / PE3 (G) / PE4 (B). V6 board.
 *
 * V6 hardware uses **common-anode** wiring: GPIO LOW lights the LED, HIGH
 * turns it off. led_write() inverts the logical bit so the rest of the
 * module can keep speaking in "1=on" terms.
 */
#include "led_rgb.h"

static LedMode  s_mode      = LED_MODE_CYCLE_RGB;
static uint8_t  s_solid_idx = 0;       /* 0=R, 1=G, 2=B for short-press cycling */
static uint32_t s_last_tick = 0;
static uint8_t  s_phase     = 0;       /* CYCLE: 0=R,1=G,2=B; BLINK: 0=on,1=off */

static void led_write(uint8_t r, uint8_t g, uint8_t b)
{
    /* Common-anode: GPIO LOW = LED on. */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, r ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, g ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4, b ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void LedRgb_Init(void)
{
    led_write(0, 0, 0);
    s_mode      = LED_MODE_CYCLE_RGB;
    s_phase     = 0;
    s_last_tick = 0;
}

void LedRgb_Set(uint8_t r, uint8_t g, uint8_t b)
{
    led_write(r ? 1 : 0, g ? 1 : 0, b ? 1 : 0);
}

void LedRgb_SetMode(LedMode m)
{
    s_mode      = m;
    s_phase     = 0;
    s_last_tick = HAL_GetTick();
    if (m == LED_MODE_OFF) led_write(0, 0, 0);
}

LedMode LedRgb_GetMode(void) { return s_mode; }

void LedRgb_NextSolidColor(void)
{
    s_solid_idx = (s_solid_idx + 1U) % 3U;
    s_mode = LED_MODE_SOLID;
    switch (s_solid_idx) {
        case 0: led_write(1, 0, 0); break;
        case 1: led_write(0, 1, 0); break;
        case 2: led_write(0, 0, 1); break;
    }
}

void LedRgb_Tick(uint32_t now_ms)
{
    /* 周期表: CYCLE 333ms/段 (1Hz 整圈), BLINK 250ms (4Hz) */
    uint32_t period;
    switch (s_mode) {
        case LED_MODE_OFF:
        case LED_MODE_SOLID:
            return;
        case LED_MODE_CYCLE_RGB:    period = 333; break;
        default:                     period = 250; break;
    }

    if ((uint32_t)(now_ms - s_last_tick) < period) return;
    s_last_tick = now_ms;

    switch (s_mode) {
        case LED_MODE_CYCLE_RGB:
            s_phase = (s_phase + 1U) % 3U;
            led_write(s_phase == 0, s_phase == 1, s_phase == 2);
            break;
        case LED_MODE_BLINK_RED:
            s_phase ^= 1U; led_write(s_phase, 0, 0); break;
        case LED_MODE_BLINK_GREEN:
            s_phase ^= 1U; led_write(0, s_phase, 0); break;
        case LED_MODE_BLINK_BLUE:
            s_phase ^= 1U; led_write(0, 0, s_phase); break;
        default: break;
    }
}
