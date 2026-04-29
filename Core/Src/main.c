/**
 * @file    Core/Src/main.c
 * @brief   Main program for V6 board (STM32F407VET6).
 *
 * Pin allocation reference: 板子/SCH_Schematic6_*.pdf "IO 未分配" page.
 * Migration plan: /Users/mac/.claude/plans/joyful-tinkering-lemur.md (阶段 1).
 *
 * Stage 1 scope: wiring init only. Business modules (fan/compressor/sensor_acq)
 * keep their old API surface but call sites in main loop are pruned to whatever
 * compiles. Real port happens in stages 2~10.
 */
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include "stm32f4xx_hal_tim.h"

#include "ina226.h"
#include "power_ctrl.h"
#include "fan_ctrl.h"
#include "compressor_ctrl.h"
#include "sensor_acq.h"
#include "buzzer.h"
#include "esp_comm.h"
#include "ota.h"
#include "led_rgb.h"
#include "button.h"
#include "sampler_comm.h"
#include "remote_ctrl.h"
#include "iot_ctrl.h"

/* ===== Peripheral handles ===== */
TIM_HandleTypeDef htim1;   /* BEEP_CTRL    PB14 TIM1_CH2N AF1 (高级定时器, 168MHz, MOE 必启) */
TIM_HandleTypeDef htim2;   /* FAN_PWM_CTRL PA15 TIM2_CH1  AF1 (84MHz, 20kHz PWM) */
TIM_HandleTypeDef htim3;   /* FAN_FB_OUT   PB4  TIM3_CH1  AF2 (84MHz, IC 1MHz tick) */
TIM_HandleTypeDef htim4;   /* MCF8329A FG  PB9  TIM4_CH4  AF2 (84MHz, IC 1MHz tick) */

/* === I2C bus scan results (temporary, stage 5 verification) === */
volatile uint8_t g_i2c1_devs[16];
volatile uint8_t g_i2c2_devs[16];
volatile uint8_t g_i2c3_devs[16];
volatile uint8_t g_i2c1_count = 0;
volatile uint8_t g_i2c2_count = 0;
volatile uint8_t g_i2c3_count = 0;


/*
 * INA226 sensor instances (V6 layout):
 *   sensors[0] — I2C1, 24V_BAT_UIP_INA226 (锂电池功率, A0/A1=GND/GND)
 *   sensors[1] — I2C1, 24V_YSJ_SENSOR_INA226 (压缩机功率, 不同 A0/A1 区分)
 *   sensors[2] — I2C2, 12V_VCC_UIP_INA226 (电瓶输入功率)
 * 实际地址 / cal_val / Rshunt 在阶段 5 按硬件标定确认。
 */
INA226_Device sensors[3];

void SystemClock_Config(void);

/* ===== TIM1: BEEP_CTRL PB14 / TIM1_CH2N AF1 ===== */
/* APB2=168MHz, TIM1 走 168MHz, PSC=167 → 1MHz tick. ARR 由 Buzzer_PlayTone 动态设置.
 * 高级定时器 — Buzzer_Init 必须 __HAL_TIM_MOE_ENABLE(), 否则 CH2N 无信号. */
void MX_TIM1_BEEP_Init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 167;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 999;       /* 默认 1kHz, ARR 由蜂鸣器动态改 */
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    oc.Pulse        = 499;                    /* 默认 50% duty */
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2);
}

/* ===== TIM2: FAN_PWM_CTRL PA15 / TIM2_CH1 AF1 ===== */
/* APB1=84MHz × 2 = 84MHz (TIMxCLK), PSC=83 → 1MHz tick, ARR=49 → 20kHz PWM.
 * Duty = CCR / (ARR+1) = CCR / 50 → percent → CCR = percent * 50 / 100. */
void MX_TIM2_FANPWM_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 83;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 49;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.Pulse      = 0;
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1);
}

/* ===== TIM3: FAN_FB_OUT PB4 / TIM3_CH1 AF2 (输入捕获) ===== */
/* APB1×2=84MHz, PSC=83 → 1MHz tick, ARR=65535 → 65.5ms 满量程. */
void MX_TIM3_FANIC_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 83;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 65535;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&htim3);

    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity  = TIM_ICPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = 0x0F;
    HAL_TIM_IC_ConfigChannel(&htim3, &ic, TIM_CHANNEL_1);

    HAL_NVIC_SetPriority(TIM3_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

/* ===== TIM4: MCF8329A FG PB9 / TIM4_CH4 AF2 (输入捕获) ===== */
/* APB1×2=84MHz, PSC=83 → 1MHz tick, ARR=65535 → 65.5ms 满量程. */
void MX_TIM4_CMPRFG_Init(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();

    htim4.Instance               = TIM4;
    htim4.Init.Prescaler         = 83;
    htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim4.Init.Period            = 65535;
    htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&htim4);

    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity  = TIM_ICPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = 0x0F;
    HAL_TIM_IC_ConfigChannel(&htim4, &ic, TIM_CHANNEL_4);

    HAL_NVIC_SetPriority(TIM4_IRQn, 3, 1);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM1_BEEP_Init();
    MX_TIM2_FANPWM_Init();
    MX_TIM3_FANIC_Init();
    MX_TIM4_CMPRFG_Init();
    MX_I2C1_Init();
    MX_I2C2_Init();
    MX_I2C3_Init();
    MX_USART1_UART_Init();   /* RemoteCtrl  stub */
    MX_USART2_UART_Init();   /* IotCtrl     stub */
    MX_USART3_UART_Init();   /* SamplerComm 阶段 7 */
    MX_USART6_UART_Init();   /* EspComm     阶段 8 */

    /* INA226 (阶段 5 标定地址 + cal_val + LSB) */
    INA226_Init(&sensors[0], &hi2c1, 0x80, 0x0355, 0.0012f);  /* 24V_BAT  A1=GND A0=GND */
    INA226_Init(&sensors[1], &hi2c1, 0x8A, 0x0355, 0.0012f);  /* 24V_YSJ  A1=VS  A0=VS  (实测) */
    INA226_Init(&sensors[2], &hi2c2, 0x80, 0x0355, 0.0012f);  /* 12V_VCC  待确认 */

    /* 业务模块 Init: 接口签名保留, 内部实现各阶段重写 */
    SensorAcq_Init();                       /* 阶段 7 改 USART3 收帧 */
    FanCtrl_Init(&htim2, &htim3);           /* 阶段 4: PWM=TIM2_CH1, IC=TIM3_CH1 */
    CompressorCtrl_Init(&htim4);            /* 阶段 6: I2C3 + FG IC=TIM4_CH4 */
    Buzzer_Init(&htim1);                    /* 阶段 2: TIM1_CH2N + MOE */
    EspComm_Init(&huart6);                  /* 阶段 8: USART6 (旧 huart2) */
    LedRgb_Init();                          /* 阶段 2: PE2/PE3/PE4 */
    Button_Init();                          /* 阶段 2: PC13 短/长按 */
    SamplerComm_Init(&huart3);              /* 阶段 7: USART3 采样板 */
    RemoteCtrl_Init(&huart1);               /* 阶段 10: stub */
    IotCtrl_Init(&huart2);                  /* 阶段 10: stub */

    PowerCtrl_StartupSequence();            /* 阶段 3: 新引脚时序 */

    /* MCF8329A 唤醒: DROFF=HIGH (driver enable), SPEED_WAKE=HIGH (out of sleep).
     * 在 sleep 模式下 MCF8329A 不响应 I2C, 必须先唤醒才能扫描到地址. */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);   /* PC12 DROFF */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0,  GPIO_PIN_SET);   /* PD0  SPEED_WAKE */
    HAL_Delay(50);                                          /* MCF8329A wake-up */

    Buzzer_PlayHajimi();                    /* 开机大疆音; 内部喂狗 */

    while (1) {
        /* IWDG 喂狗 — Bootloader 启了, App 必须续 */
        IWDG->KR = 0xAAAAU;

        uint32_t now_ms = HAL_GetTick();

        /* === 高频 (~10ms 节拍) === */
        LedRgb_Tick(now_ms);
        switch (Button_Poll()) {
            case BUTTON_EVT_SHORT:
                LedRgb_NextSolidColor();
                Buzzer_PlayTone(2000, 50);
                break;
            case BUTTON_EVT_LONG:
                LedRgb_SetMode(LED_MODE_OFF);
                Buzzer_PlayTone(800, 500);
                break;
            default: break;
        }
        EspComm_Poll();
        SamplerComm_Poll();
        SensorAcq_Tick(now_ms);
        RemoteCtrl_Poll();
        IotCtrl_Poll();

        if (EspComm_TakeOtaSelfTestRequest()) {
            Ota_SelfTest(32U * 1024U);
        }
        if (OtaProto_TakeResetRequest()) {
            HAL_Delay(50);
            __DSB();
            NVIC_SystemReset();
        }

        /* === 100ms 分流: INA226 / Gear cmd === */
        {
            static uint32_t s_last_100 = 0;
            if ((uint32_t)(now_ms - s_last_100) >= 100U) {
                s_last_100 = now_ms;

                for (int i = 0; i < 3; i++) {
                    INA226_UpdateData(&sensors[i]);
                }

                EspComm_GearCmd *cmd = EspComm_GetGearCmd();
                if (cmd->updated) {
                    cmd->updated = 0;
                    if (cmd->on) {
                        FanCtrl_Enable(1);
                        FanCtrl_SetDuty(100);
                        PowerCtrl_EnablePump(1);

                        uint8_t duty = 110 - cmd->gear * 10;
                        if (duty > 100) duty = 100;
                        if (duty < 10)  duty = 10;
                        CompressorCtrl_SetBrake(0);
                        CompressorCtrl_SetDuty(duty);
                    } else {
                        FanCtrl_Enable(0);
                        FanCtrl_SetDuty(0);
                        PowerCtrl_EnablePump(0);

                        CompressorCtrl_SetDuty(0);
                        CompressorCtrl_SetBrake(1);
                    }
                }
            }
        }

        /* === 500ms 分流: 采样板心跳, OTA mark boot === */
        {
            static uint32_t s_last_500 = 0;
            if ((uint32_t)(now_ms - s_last_500) >= 500U) {
                s_last_500 = now_ms;

                static uint8_t  s_hb_seq = 0;
                SamplerComm_SendHeartbeat(s_hb_seq++);

                static uint8_t s_boot_marked = 0;
                if (!s_boot_marked && now_ms >= 10000U) {
                    s_boot_marked = 1;
                    (void)Ota_MarkBootOk();
                }
            }
        }

        /* === 1s 分流: Status 帧 === */
        {
            static uint32_t s_last_1000 = 0;
            if ((uint32_t)(now_ms - s_last_1000) >= 1000U) {
                s_last_1000 = now_ms;

                float ambient_c = SensorAcq_NTCToCelsius(SensorAcq_GetNTC(7));
                float v_24in    = INA226_GetVoltage(&sensors[2]);
                float p_24in    = INA226_GetPower(&sensors[2]);
                float v_bat     = INA226_GetVoltage(&sensors[0]);
                int16_t pct = (int16_t)((v_bat - 15.0f) / (24.8f - 15.0f) * 100.0f);
                if (pct > 100) pct = 100;
                if (pct < 0)   pct = 0;

                static uint16_t ota_ramp = 0;
                int16_t ramp_lo, ramp_span;
                if (RTC->BKP0R == 0xB0B0B0B0U) { ramp_lo = 10; ramp_span = 11; }
                else                            { ramp_lo = 1;  ramp_span = 10; }
                int16_t ramp_val = ramp_lo + (int16_t)(ota_ramp % (uint16_t)ramp_span);
                ota_ramp++;

                EspComm_Status st;
                st.water_temp_x10   = ramp_val;
                st.battery_pct      = (uint8_t)pct;
                st.total_power_w    = (uint16_t)p_24in;
                st.error_flags      = 0;
                st.ambient_temp_x10 = (int16_t)(ambient_c * 10.0f);
                (void)v_24in;

                if (!OtaProto_IsBusy()) {
                    EspComm_SendStatus(&st);
                }
            }
        }

        HAL_Delay(10);
    }
}

void SystemClock_Config(void)
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
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

/* HAL timer callbacks → 业务模块 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    FanCtrl_CaptureCallback(htim);
    CompressorCtrl_CaptureCallback(htim);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    FanCtrl_OverflowCallback(htim);
    CompressorCtrl_OverflowCallback(htim);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { (void)file; (void)line; }
#endif
