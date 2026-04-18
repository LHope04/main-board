/**
 * @file    bootloader/Src/main.c
 * @brief   Bootloader (Phase 7): rollback-aware A/B boot with IWDG.
 *
 * Flash layout:
 *   0x08000000  Bootloader (this image, 32KB)
 *   0x08008000  Boot Params A (16KB, sector 2)
 *   0x0800C000  Boot Params B (16KB, sector 3)
 *   0x08020000  App Slot A (128KB, sector 5)
 *   0x08040000  App Slot B (128KB, sector 6)
 *
 * Decision policy (see OTA_PLAN.md Phase 7):
 *   - pending_update == 0 → boot selected slot unconditionally (no writes,
 *     no CRC check) so debug sessions that hang the App never roll back.
 *   - pending_update == 1 → this is the "confirm-once" window after an OTA.
 *       * boot_count++ (written back first, so even a crash mid-flash still
 *         bumps the counter on the next reset).
 *       * If target slot CRC32 doesn't match, or boot_count >= MAX_ATTEMPTS,
 *         flip boot_slot, clear pending_update, zero boot_count, write back,
 *         and boot the other slot.
 *       * Otherwise, try the target slot again.
 *   - App clears pending_update / boot_count via Ota_MarkBootOk() after
 *     running stably for BOOT_OK_DELAY_MS.
 *
 * Silent on USART2 — that line is owned by the App/ESP32 protocol, any
 * stray bytes would desync the ESP. Status is reported via LEDs only:
 *   LED1 (PD13) flash count = 1 fallback (no params), 2 jump A, 3 jump B,
 *                             4 rollback fired
 *   LED2 (PD14): blinks forever if the jump was aborted (bad MSP)
 */

#include "stm32f4xx_hal.h"
#include "boot_params.h"
#include "crc32.h"

/* Magic values the App reads from RTC->BKP0R to know which slot the
 * Bootloader decided to launch. Values are arbitrary but distinct. */
#define BOOT_MARK_A         0xA0A0A0A0U
#define BOOT_MARK_B         0xB0B0B0B0U

/* Confirm-once policy: a freshly OTA'd slot gets this many attempts before
 * we give up and flip back to the previously-known-good slot. */
#define MAX_BOOT_ATTEMPTS   3U

/* IWDG: LSI ~32 kHz, prescaler /256 → ~125 Hz tick → RLR=500 ≈ 4 s.
 * Enough headroom for a worst-case params sector erase (~500ms). */
#define IWDG_PRESCALER_256  0x06U
#define IWDG_RELOAD_4S      500U

static void SystemClock_Config(void);
static void GPIO_Init(void);
static void jump_to_app(uint32_t app_addr);
static void blink_led1(uint8_t count);
static void publish_boot_mark(uint32_t mark);
static void iwdg_start(void);
static void iwdg_refresh(void);
static bool slot_crc_ok(const boot_params_t *p, uint32_t slot);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    iwdg_start();

    boot_params_t params;
    uint32_t app_addr;
    uint8_t blink;
    uint32_t mark;

    if (!BootParams_Read(&params)) {
        /* No valid params at all — first boot or both copies corrupt.
         * Fall back to Slot A without touching flash. */
        app_addr = BOOT_SLOT_A_ADDR;
        blink    = 1;
        mark     = BOOT_MARK_A;
    } else if (params.pending_update == 0U) {
        /* Steady state. No writes, no CRC check — lets ST-Link debug
         * sessions hang the App without ever tripping rollback. */
        app_addr = BootParams_SelectSlotAddr(&params);
        blink    = (params.boot_slot == BOOT_SLOT_B) ? 3 : 2;
        mark     = (params.boot_slot == BOOT_SLOT_B) ? BOOT_MARK_B : BOOT_MARK_A;
    } else {
        /* Confirm-once window after an OTA. Count this attempt first, then
         * decide whether the target slot still deserves another try. */
        params.boot_count += 1U;

        bool rollback = (params.boot_count >= MAX_BOOT_ATTEMPTS)
                      || !slot_crc_ok(&params, params.boot_slot);

        if (rollback) {
            params.boot_slot      = (params.boot_slot == BOOT_SLOT_B) ? BOOT_SLOT_A : BOOT_SLOT_B;
            params.pending_update = 0U;
            params.boot_count     = 0U;
            blink                 = 4;
        } else {
            blink = (params.boot_slot == BOOT_SLOT_B) ? 3 : 2;
        }

        iwdg_refresh();
        (void)BootParams_Write(&params);
        iwdg_refresh();

        app_addr = BootParams_SelectSlotAddr(&params);
        mark     = (params.boot_slot == BOOT_SLOT_B) ? BOOT_MARK_B : BOOT_MARK_A;
    }

    publish_boot_mark(mark);
    blink_led1(blink);
    iwdg_refresh();

    jump_to_app(app_addr);

    while (1) {
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
        HAL_Delay(200);
        iwdg_refresh();
    }
}

/* Verify the image at the given slot against slot_crc32[slot].
 * Returns true if the params say the slot is valid AND the CRC matches.
 * An "unvalidated" slot (slot_valid==0) conservatively returns true — that
 * covers the first-boot case where Slot A was flashed by ST-Link without
 * any OTA-style CRC bookkeeping. */
static bool slot_crc_ok(const boot_params_t *p, uint32_t slot)
{
    if (slot > 1U) return false;
    if (p->slot_valid[slot] == 0U) return true;
    if (p->slot_size[slot] == 0U || p->slot_size[slot] > 0x20000U) return false;

    uint32_t base = (slot == BOOT_SLOT_B) ? BOOT_SLOT_B_ADDR : BOOT_SLOT_A_ADDR;
    uint32_t crc  = crc32_compute((const void *)base, p->slot_size[slot]);
    return crc == p->slot_crc32[slot];
}

static void jump_to_app(uint32_t app_addr)
{
    uint32_t msp = *(volatile uint32_t *)app_addr;
    uint32_t pc  = *(volatile uint32_t *)(app_addr + 4);

    /* Sanity: MSP must point into SRAM */
    if ((msp & 0x2FFE0000U) != 0x20000000U) {
        return;
    }

    /* Mask interrupts during the transition only (restored before branch) */
    __disable_irq();

    HAL_RCC_DeInit();
    HAL_DeInit();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    for (uint32_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = app_addr;
    __DSB();
    __ISB();
    __set_MSP(msp);

    __enable_irq();

    ((void (*)(void))pc)();
}

static void publish_boot_mark(uint32_t mark)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP0R = mark;
}

static void blink_led1(uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(80);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_Delay(120);
        iwdg_refresh();
    }
}

/* IWDG raw-register control — HAL_IWDG module isn't enabled in hal_conf.h
 * and we want to keep this change minimal. Once started, IWDG cannot be
 * disabled by software; the App must continue refreshing it. */
static void iwdg_start(void)
{
    IWDG->KR  = 0x5555U;              /* unlock PR/RLR */
    IWDG->PR  = IWDG_PRESCALER_256;
    IWDG->RLR = IWDG_RELOAD_4S;
    IWDG->KR  = 0xAAAAU;              /* refresh */
    IWDG->KR  = 0xCCCCU;              /* start */
}

static void iwdg_refresh(void)
{
    IWDG->KR = 0xAAAAU;
}

static void GPIO_Init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef gi = {0};
    gi.Pin   = GPIO_PIN_13 | GPIO_PIN_14;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &gi);

    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);
}

/* Matches App's clock config: HSE 25MHz → PLL → 168MHz */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 4;
    osc.PLL.PLLN       = 168;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 4;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

/* HAL_Delay relies on SysTick being pumped */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
