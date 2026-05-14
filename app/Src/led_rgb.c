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
        case LED_MODE_BREATH:       /* PWM 全在 LedRgb_PwmTick (TIM7 ISR) 里跑 */
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

/* ===== 白色呼吸灯软 PWM =====
 * LedRgb_PwmTick() 由 TIM7 ISR @2kHz 调用.
 *   - PWM 周期 = 32 tick (2kHz/32 = 62.5Hz 刷新, 无可见闪烁)
 *   - 每个 PWM 周期末尾把亮度 ±1, 在 0..31 之间三角波往返
 *   - 一个完整呼吸 = 64 步 × 16ms ≈ 1s 亮 + 1s 暗 ≈ 2s/次
 * 非 BREATH 模式时此函数直接返回, 不影响其它模式. */
#define BREATH_PWM_PERIOD  32U
#define BREATH_MAX_LEVEL   31U

void LedRgb_PwmTick(void)
{
    static uint8_t pwm_cnt   = 0;
    static uint8_t level     = 0;
    static int8_t  dir       = 1;

    if (s_mode != LED_MODE_BREATH) return;

    /* PWM: level 决定一周期内点亮多少 tick */
    led_write(pwm_cnt < level, pwm_cnt < level, pwm_cnt < level);

    if (++pwm_cnt >= BREATH_PWM_PERIOD) {
        pwm_cnt = 0;
        /* 三角波亮度: 0→31→0 */
        if (dir > 0) {
            if (level >= BREATH_MAX_LEVEL) { level = BREATH_MAX_LEVEL; dir = -1; }
            else level++;
        } else {
            if (level == 0) { dir = 1; }
            else level--;
        }
    }
}
