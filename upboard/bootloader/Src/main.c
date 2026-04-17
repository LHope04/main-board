/**
 * @file    bootloader/Src/main.c
 * @brief   Bootloader (Phase 3): reads A/B param block and jumps to the active slot.
 *
 * Flash layout:
 *   0x08000000  Bootloader (this image, 32KB)
 *   0x08008000  Boot Params A (16KB, sector 2)
 *   0x0800C000  Boot Params B (16KB, sector 3)
 *   0x08020000  App Slot A (128KB, sector 5)
 *   0x08040000  App Slot B (128KB, sector 6)
 *
 * Silent on USART2 — that line is owned by the App/ESP32 protocol, any
 * stray bytes would desync the ESP. Status is reported via LEDs only:
 *   LED1 (PD13) flash count = 1 fallback (no params), 2 jump A, 3 jump B
 *   LED2 (PD14): blinks forever if the jump was aborted (bad MSP)
 */

#include "stm32f4xx_hal.h"
#include "boot_params.h"

/* Magic values the App reads from RTC->BKP0R to know which slot the
 * Bootloader decided to launch. Values are arbitrary but distinct. */
#define BOOT_MARK_A   0xA0A0A0A0U
#define BOOT_MARK_B   0xB0B0B0B0U

static void SystemClock_Config(void);
static void GPIO_Init(void);
static void jump_to_app(uint32_t app_addr);
static void blink_led1(uint8_t count);
static void publish_boot_mark(uint32_t mark);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();

    boot_params_t params;
    uint32_t app_addr;
    uint8_t blink;
    uint32_t mark;

    if (BootParams_Read(&params)) {
        app_addr = BootParams_SelectSlotAddr(&params);
        blink    = (params.boot_slot == BOOT_SLOT_B) ? 3 : 2;
        mark     = (params.boot_slot == BOOT_SLOT_B) ? BOOT_MARK_B : BOOT_MARK_A;
    } else {
        app_addr = BOOT_SLOT_A_ADDR;
        blink    = 1;
        mark     = BOOT_MARK_A;
    }

    publish_boot_mark(mark);
    blink_led1(blink);

    jump_to_app(app_addr);

    while (1) {
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
        HAL_Delay(200);
    }
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
    }
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
