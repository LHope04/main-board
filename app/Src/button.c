/**
 * @file    app/Src/button.c
 * @brief   Front-panel button (PC13) — debounced short/long-press detection.
 *
 * State machine (polled at ~10ms cadence is safe; 20ms debounce window):
 *   IDLE          → DEBOUNCING (on first LOW reading)
 *   DEBOUNCING    → PRESSED   (LOW persists ≥ 20ms)
 *                 → IDLE      (bounce; reading went HIGH again)
 *   PRESSED       → LONG_HELD (still LOW after 1000ms; emit BUTTON_EVT_LONG)
 *                 → IDLE      (release; emit BUTTON_EVT_SHORT)
 *   LONG_HELD     → IDLE      (release; no extra event — already emitted)
 *
 * Long event is emitted exactly once when threshold is crossed, not on
 * release. This keeps the long-press feel responsive ("press and hold for
 * a second to do X" — the action fires the moment the second elapses).
 */
#include "button.h"

#define DEBOUNCE_MS   20U
#define LONG_PRESS_MS 1000U

typedef enum {
    BTN_IDLE,
    BTN_DEBOUNCING,
    BTN_PRESSED,
    BTN_LONG_HELD,
} btn_state_t;

static btn_state_t s_state    = BTN_IDLE;
static uint32_t    s_t_change = 0;       /* tick when state last changed */

/* === Debug snapshots — readable via SWD mdw === */
volatile uint32_t g_btn_poll_cnt    = 0;   /* Poll 调用次数 */
volatile uint8_t  g_btn_last_raw    = 0;   /* 上次 raw 读值 (1=pressed) */
volatile uint8_t  g_btn_state_snap  = 0;   /* btn_state_t 当前快照 */
volatile uint32_t g_btn_short_cnt   = 0;
volatile uint32_t g_btn_long_cnt    = 0;

static uint8_t button_is_pressed_raw(void)
{
    /* PC13 active LOW with internal pull-up */
    return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET;
}

void Button_Init(void)
{
    s_state    = BTN_IDLE;
    s_t_change = HAL_GetTick();
}

ButtonEvt Button_Poll(void)
{
    uint32_t now     = HAL_GetTick();
    uint8_t  pressed = button_is_pressed_raw();

    g_btn_poll_cnt++;
    g_btn_last_raw   = pressed;
    g_btn_state_snap = (uint8_t)s_state;

    switch (s_state) {
        case BTN_IDLE:
            if (pressed) { s_state = BTN_DEBOUNCING; s_t_change = now; }
            break;

        case BTN_DEBOUNCING:
            if (!pressed) {                 /* bounced back */
                s_state = BTN_IDLE; s_t_change = now;
            } else if ((uint32_t)(now - s_t_change) >= DEBOUNCE_MS) {
                s_state = BTN_PRESSED; s_t_change = now;
            }
            break;

        case BTN_PRESSED:
            if (!pressed) {
                s_state = BTN_IDLE; s_t_change = now;
                g_btn_short_cnt++;
                return BUTTON_EVT_SHORT;
            }
            if ((uint32_t)(now - s_t_change) >= LONG_PRESS_MS) {
                s_state = BTN_LONG_HELD; s_t_change = now;
                g_btn_long_cnt++;
                return BUTTON_EVT_LONG;
            }
            break;

        case BTN_LONG_HELD:
            if (!pressed) { s_state = BTN_IDLE; s_t_change = now; }
            break;
    }
    return BUTTON_EVT_NONE;
}

uint8_t Button_ReadAcc(void)
{
    /* PC14 ACC: assume same active-LOW convention as PC13 */
    return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_14) == GPIO_PIN_RESET;
}
