/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include "stm32f4xx_hal_tim.h"
#include "stm32f4xx_hal_adc.h"

/* USER CODE BEGIN Includes */
#include "ina226.h"
#include "power_ctrl.h"
#include "fan_ctrl.h"
#include "compressor_ctrl.h"
#include "sensor_acq.h"
#include "buzzer.h"
#include "esp_comm.h"
#include "ota.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* Peripheral handles — initialised by MX_*_Init below */
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim8;
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/*
 * INA226 sensor instances (application-level, not driver-internal):
 *   sensors[0] — I2C1, pump       (Rshunt=100mΩ, LSB=0.1mA)
 *   sensors[1] — I2C2, 24V input  (Rshunt=6mΩ,   LSB=1.2mA)
 *   sensors[2] — I2C3, total      (Rshunt=6mΩ,   LSB=1.2mA)
 */
INA226_Device sensors[3];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ADC1 + DMA2 init for 8-channel NTC scan (PA0~PA7, IN0~IN7) */
void MX_ADC1_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_adc1.Instance                 = DMA2_Stream0;
    hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_CIRCULAR;
    hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_adc1);
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.ContinuousConvMode    = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 8;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    HAL_ADC_Init(&hadc1);

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    uint32_t channels[8] = {
        ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
        ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7
    };
    for (int i = 0; i < 8; i++) {
        sConfig.Channel = channels[i];
        sConfig.Rank    = i + 1;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    }
}

/* TIM2 PWM init for passive buzzer (PA15, TIM2_CH1 AF1)
 * APB1=84MHz, PSC=83 → 1MHz tick; ARR updated per tone by buzzer module */
void MX_TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 83;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 999;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    TIM_OC_InitTypeDef sConfig = {0};
    sConfig.OCMode     = TIM_OCMODE_PWM1;
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;
    sConfig.Pulse      = 499;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfig, TIM_CHANNEL_1);
}

/* TIM3 init: 20kHz PWM on CH2(PB5 YSJ) and CH3(PC8 FAN),
 *            input capture on CH1(PB4 SC_COUNT)
 * APB1=84MHz, PSC=83 → 1MHz tick, ARR=49 → 20kHz */
void MX_TIM3_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 83;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 49;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    TIM_OC_InitTypeDef sConfig = {0};
    sConfig.OCMode     = TIM_OCMODE_PWM1;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;

    /* CH2: YSJ compressor — PMOS inverted, init off (Pulse=ARR → output HIGH → PMOS off) */
    sConfig.OCPolarity = TIM_OCPOLARITY_LOW;
    sConfig.Pulse      = 49;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfig, TIM_CHANNEL_2);

    /* CH3: FAN — init off */
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.Pulse      = 0;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfig, TIM_CHANNEL_3);

    /* CH1: SC_COUNT input capture — rising edge, filter 0x0F */
    TIM_IC_InitTypeDef icConfig = {0};
    icConfig.ICPolarity  = TIM_ICPOLARITY_RISING;
    icConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    icConfig.ICPrescaler = TIM_ICPSC_DIV1;
    icConfig.ICFilter    = 0x0F;
    HAL_TIM_IC_ConfigChannel(&htim3, &icConfig, TIM_CHANNEL_1);

    HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

/* TIM8 input capture: FAN_FB (PC7, CH2 AF3)
 * APB2=168MHz, PSC=167 → 1MHz tick, ARR=65535 → 65.5ms per overflow */
void MX_TIM8_Init(void)
{
    __HAL_RCC_TIM8_CLK_ENABLE();

    htim8.Instance                = TIM8;
    htim8.Init.Prescaler          = 167;
    htim8.Init.CounterMode        = TIM_COUNTERMODE_UP;
    htim8.Init.Period             = 65535;
    htim8.Init.ClockDivision      = TIM_CLOCKDIVISION_DIV1;
    htim8.Init.RepetitionCounter  = 0;
    htim8.Init.AutoReloadPreload  = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&htim8);

    TIM_IC_InitTypeDef icConfig = {0};
    icConfig.ICPolarity  = TIM_ICPOLARITY_RISING;
    icConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    icConfig.ICPrescaler = TIM_ICPSC_DIV1;
    icConfig.ICFilter    = 0x0F;   /* suppress 20kHz PWM crosstalk from PC8 */
    HAL_TIM_IC_ConfigChannel(&htim8, &icConfig, TIM_CHANNEL_2);

    /* TIM8 has separate IRQ vectors for update and capture */
    HAL_NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
    HAL_NVIC_SetPriority(TIM8_CC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM8_CC_IRQn);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM8_Init();
  MX_ADC1_Init();
  MX_I2C2_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */

  /* INA226: 压缩机(I2C1), 24V 电瓶(I2C2), 锂电池(I2C3) */
  INA226_Init(&sensors[0], &hi2c1, 0x88, 0x0200, 0.0001f);
  INA226_Init(&sensors[1], &hi2c2, 0x88, 0x0355, 0.0012f);
  INA226_Init(&sensors[2], &hi2c3, 0x88, 0x0355, 0.0012f);

  SensorAcq_Init(&hadc1);
  FanCtrl_Init(&htim3, &htim8);
  CompressorCtrl_Init(&htim3);
  Buzzer_Init(&htim2);
  EspComm_Init(&huart2);

  PowerCtrl_StartupSequence();

  SensorAcq_Start();

  Buzzer_PlayHajimi();

  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */

    /* Update INA226 power measurements */
    for (int i = 0; i < 3; i++) {
        INA226_UpdateData(&sensors[i]);
    }

    /* Process ESP32 commands */
    EspComm_Poll();

    /* OTA self-test trigger — C3 sends ESP_CMD_OTA_SELFTEST (0xF0).
     * Copies current App from Slot A to Slot B, switches active_slot=B,
     * and resets. Does not return on success. */
    if (EspComm_TakeOtaSelfTestRequest()) {
        Ota_SelfTest(32U * 1024U);
    }

    /* Apply gear/on command to actuators */
    EspComm_GearCmd *cmd = EspComm_GetGearCmd();
    if (cmd->updated) {
        cmd->updated = 0;
        if (cmd->on) {
            /* Fan & pump: full power, on/off only */
            FanCtrl_Enable(1);
            FanCtrl_SetDuty(100);
            PowerCtrl_EnablePump(1);

            /* Compressor: gear 1~10 → duty 100%~10% (PMOS inverted) */
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

    /* Send status to ESP32 every 1s (2 x 500ms loops) */
    {
        static uint8_t tx_div = 0;
        if (++tx_div >= 2) {
            tx_div = 0;

            float water_c   = SensorAcq_NTCToCelsius(SensorAcq_GetNTC(3));  /* 2-NTC1 水温 */
            float ambient_c = SensorAcq_NTCToCelsius(SensorAcq_GetNTC(7)); /* 1-NTC1 环温 */

            /* 24V电瓶 (I2C2): 发送电压和功率 */
            float v_24in = INA226_GetVoltage(&sensors[1]);
            float p_24in = INA226_GetPower(&sensors[1]);

            /* 锂电池 (I2C3): 电压→电量，满电24.8V=100%，15V=0% */
            float v_bat  = INA226_GetVoltage(&sensors[2]);
            int16_t pct  = (int16_t)((v_bat - 15.0f) / (24.8f - 15.0f) * 100.0f);
            if (pct > 100) pct = 100;
            if (pct < 0)   pct = 0;

            /* OTA liveness signal: water_temp_x10 carries a ramp whose range
             * depends on the slot the Bootloader decided to launch. Bootloader
             * stashes 0xA0A0A0A0 (A) or 0xB0B0B0B0 (B) in RTC->BKP0R before
             * jumping, so this works even when the two slots share bytes.
             *   Slot A → 1..10
             *   Slot B → 10..20
             * Value not changing = App has hung. */
            static uint16_t ota_ramp = 0;
            int16_t ramp_lo, ramp_span;
            if (RTC->BKP0R == 0xB0B0B0B0U) { ramp_lo = 10; ramp_span = 11; }
            else                           { ramp_lo = 1;  ramp_span = 10; }
            int16_t ramp_val = ramp_lo + (int16_t)(ota_ramp % (uint16_t)ramp_span);
            ota_ramp++;

            EspComm_Status st;
            st.water_temp_x10   = ramp_val;                    /* OTA liveness ramp */
            st.battery_pct      = (uint8_t)pct;                /* 锂电池电量 0~100% */
            st.total_power_w    = (uint16_t)p_24in;            /* 24V电瓶功率 W */
            st.error_flags      = 0;
            st.ambient_temp_x10 = (int16_t)(ambient_c * 10.0f);
            (void)v_24in;

            EspComm_SendStatus(&st);
        }
    }

    HAL_Delay(500);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 4;
  RCC_OscInitStruct.PLL.PLLN       = 168;
  RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ       = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* HAL timer callbacks — delegate to modules */
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

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
