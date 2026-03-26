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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#include <stdio.h>


/* 使用 Keil (MDK) */
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)


PUTCHAR_PROTOTYPE
{
    
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 10);
    return ch;
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
typedef struct {
    I2C_HandleTypeDef *hi2c; // I2C句柄
    uint16_t dev_addr;       // 设备地址 (已经左移一位后的地址)
    uint16_t cal_val;        // 校准寄存器(0x05)的值
    float cur_lsb;           // 该设备对应的 Current_LSB
    float v, i, p;           // 测量结果
} INA226_Device;

/* * 根据电路图初始化：
 * 每路IIC各一个设备，地址均为 0x40 (二进制 1000000)，HAL左移一位后为 0x80
 * I2C1: Rshunt = 0.1 Ohm.   设 LSB=0.1mA(0.0001), CAL = 0.00512/(0.0001*0.1) = 512 (0x0200)
 * I2C2: Rshunt = 0.006 Ohm. 设 LSB=1mA(0.001), CAL = 0.00512/(0.001*0.006) = 853 (0x0355)
 * I2C3: Rshunt = 0.006 Ohm. 设 LSB=1mA(0.001), CAL = 0.00512/(0.001*0.006) = 853 (0x0355)
 */
INA226_Device sensors[3] = {
    // I2C1 (Rshunt = 100mr, LSB = 0.1mA)
    {&hi2c1, 0x88, 0x0200, 0.0001f}, // v1: 水泵

    // I2C2 (Rshunt = 6mr, LSB = 1mA)
    {&hi2c2, 0x88, 0x0355, 0.0012f},  // v2: 24v输入

    // I2C3 (Rshunt = 6mr, LSB = 1mA)
    {&hi2c3, 0x88, 0x0355, 0.0012f},  // v3: 输入总功率
};

// 通用写寄存器
void INA226_Write(INA226_Device *dev, uint8_t reg, uint16_t data) {
    uint8_t buf[2] = { (data >> 8) & 0xFF, data & 0xFF };
    HAL_I2C_Mem_Write(dev->hi2c, dev->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
}

// 通用读寄存器
uint16_t INA226_Read(INA226_Device *dev, uint8_t reg) {
    uint8_t buf[2];
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100) == HAL_OK) {
        return (uint16_t)((buf[0] << 8) | buf[1]);
    }
    return 0;
}

// 核心：初始化函数
void INA226_InitDevice(INA226_Device *dev) {
    // 1. 配置寄存器 (0x00): 16次平均, 1.1ms采样, 连续测量
    INA226_Write(dev, 0x00, 0x4527);
    
    // 2. 写入该设备特有的校准值 (0x05)
    INA226_Write(dev, 0x05, dev->cal_val);
}

// 核心：读取数据并计算
void INA226_UpdateData(INA226_Device *dev) {
    // 电压：固定 1.25mV/LSB
    dev->v = (float)INA226_Read(dev, 0x02) * 0.00125f;

    // 电流：原始值 * 该设备的 LSB
    int16_t i_raw = (int16_t)INA226_Read(dev, 0x04);
    dev->i = (float)i_raw * dev->cur_lsb;

    // 功率：固定 25 * 电流LSB
    dev->p = (float)INA226_Read(dev, 0x03) * (dev->cur_lsb * 25.0f);
}


/* TIM2 handle for BEEP_CTRL (PA15, CH1) passive buzzer */
TIM_HandleTypeDef htim2;

/* TIM3 handle for FAN_PWM_CTRL (PC8, CH3) and YSJ_PWM (PB5, CH2) */
TIM_HandleTypeDef htim3;

/* SC_COUNT input capture (TIM3_CH1, PB4) */
volatile uint32_t ic_val1       = 0;
volatile uint32_t ic_val2       = 0;
volatile uint32_t ic_overflow   = 0;
volatile uint8_t  ic_state      = 0;   /* 0=waiting first edge, 1=first captured */
volatile uint32_t sc_freq_hz    = 0;   /* compressor feedback frequency in Hz */

/* TIM8 handle for FAN_FB input capture (PC7, CH2) */
TIM_HandleTypeDef htim8;

/* FAN_FB input capture variables */
volatile uint32_t fan_ic_val1     = 0;
volatile uint32_t fan_ic_val2     = 0;
volatile uint32_t fan_ic_overflow = 0;
volatile uint8_t  fan_ic_state    = 0;
volatile uint32_t fan_freq_hz     = 0;   /* fan feedback frequency in Hz */

/* ADC1 + DMA for 8-ch NTC scan */
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
uint16_t adc_buf[8];  /* [IN0..IN7] = [2-NTC2,2-NTC3,2-NTC4,2-NTC1,1-NTC2,1-NTC3,1-NTC4,1-NTC1] */

void MX_ADC1_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* DMA2 Stream0 Channel0 for ADC1 */
    hdma_adc1.Instance = DMA2_Stream0;
    hdma_adc1.Init.Channel = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_LOW;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_adc1);
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = ENABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 8;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    HAL_ADC_Init(&hadc1);

    /* Configure 8 channels in scan order */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    uint32_t channels[8] = {
        ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
        ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7
    };
    for (int i = 0; i < 8; i++) {
        sConfig.Channel = channels[i];
        sConfig.Rank = i + 1;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    }
}

/* TIM2 PWM init for passive buzzer on PA15 (TIM2_CH1)           */
/* APB1 timer clock = 84MHz, PSC=83 → 1MHz tick, ARR adjustable */
void MX_TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 83;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 999;           /* placeholder, updated per note */
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim2);

    TIM_OC_InitTypeDef sConfig = {0};
    sConfig.OCMode     = TIM_OCMODE_PWM1;
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;
    sConfig.Pulse      = 499;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfig, TIM_CHANNEL_1);
}

/* DJI drone startup chime                                        */
/*  Phase 1: 3 short ESC-style beeps (C5, both LEDs blink)       */
/*  Phase 2: ascending chime A5→C#6→E6 (LED on per note)         */
static void Play_DJI_Startup(void)
{
    /* ESC init beeps */
    static const uint16_t esc_notes[] = { 523, 0, 523, 0, 523, 0 };
    static const uint16_t esc_durs[]  = { 100,80, 100,80, 100,150 };
    for (uint32_t i = 0; i < 6; i++) {
        if (esc_notes[i]) {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_SET);
            uint32_t arr = 1000000UL / esc_notes[i] - 1U;
            __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, arr / 2U);
            HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
        } else {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_RESET);
            HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
        }
        HAL_Delay(esc_durs[i]);
    }
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_Delay(100);

    /* Main chime: A5(880) → C#6(1109) → E6(1319) */
    static const uint16_t chime_notes[] = { 880, 1109, 1319 };
    static const uint16_t chime_durs[]  = { 200,  200,  650 };
    for (uint32_t i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_SET);
        uint32_t arr = 1000000UL / chime_notes[i] - 1U;
        __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, arr / 2U);
        HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
        HAL_Delay(chime_durs[i]);
        HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
        HAL_Delay(30);
    }
    /* both LEDs stay on after startup */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_SET);
}

/* TIM3 init: 20kHz PWM on CH2(PB5 YSJ) and CH3(PC8 FAN),       */
/*            input capture on CH1(PB4 SC_COUNT)                  */
/* APB1 timer clock = 84MHz, PSC=83 → 1MHz tick, ARR=49 → 20kHz */
void MX_TIM3_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 83;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 49;           /* 20kHz: 1MHz / 50 = 20kHz */
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    TIM_OC_InitTypeDef sConfig = {0};
    sConfig.OCMode = TIM_OCMODE_PWM1;
    sConfig.OCFastMode = TIM_OCFAST_DISABLE;

    /* CH2: YSJ compressor — PMOS inverted, polarity LOW, init duty 0% (stopped) */
    sConfig.OCPolarity = TIM_OCPOLARITY_LOW;
    sConfig.Pulse = 0;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfig, TIM_CHANNEL_2);

    /* CH3: FAN — init duty 50% (25/50) */
    sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfig.Pulse = 25;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfig, TIM_CHANNEL_3);

    /* CH1: SC_COUNT input capture — rising edge, direct TI, filter 0x0F */
    TIM_IC_InitTypeDef icConfig = {0};
    icConfig.ICPolarity  = TIM_ICPOLARITY_RISING;
    icConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    icConfig.ICPrescaler = TIM_ICPSC_DIV1;
    icConfig.ICFilter    = 0x0F;
    HAL_TIM_IC_ConfigChannel(&htim3, &icConfig, TIM_CHANNEL_1);

    /* Enable TIM3 IRQ for both capture and overflow */
    HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

/* TIM8 input capture: FAN_FB(PC7, CH2)                              */
/* APB2 timer clock = 168MHz, PSC=167 → 1MHz tick, ARR=65535        */
void MX_TIM8_Init(void)
{
    __HAL_RCC_TIM8_CLK_ENABLE();

    htim8.Instance = TIM8;
    htim8.Init.Prescaler = 167;
    htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim8.Init.Period = 65535;           /* 65.5ms per overflow */
    htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim8.Init.RepetitionCounter = 0;
    htim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&htim8);

    TIM_IC_InitTypeDef icConfig = {0};
    icConfig.ICPolarity  = TIM_ICPOLARITY_RISING;
    icConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    icConfig.ICPrescaler = TIM_ICPSC_DIV1;
    icConfig.ICFilter    = 0x0F;   /* max filter — suppress 20kHz PWM crosstalk */
    HAL_TIM_IC_ConfigChannel(&htim8, &icConfig, TIM_CHANNEL_2);

    /* TIM8 uses separate IRQs for update and capture */
    HAL_NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
    HAL_NVIC_SetPriority(TIM8_CC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM8_CC_IRQn);
}

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */





/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
int iii = 0;

float v_bus, current, power;
// 定义两个设备的地址（对应你硬件上A0/A1的接法）

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
//#define INA226_ADDR 0x80 // 假设 A0, A1 接地
//void INA226_WriteReg(uint8_t reg_addr, uint16_t data) {
//    uint8_t buf[2];
//    buf[0] = (data >> 8) & 0xFF; // 高字节
//    buf[1] = data & 0xFF;        // 低字节
//	HAL_I2C_Mem_Write(&hi2c1, INA226_ADDR , reg_addr, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
//}

//// 读取 16 位寄存器的通用函数
//uint16_t INA226_ReadReg(uint8_t reg_addr) {
//    uint8_t data[2];
//    // 使用 HAL_I2C_Mem_Read 读取 2 个字节
//    if (HAL_I2C_Mem_Read(&hi2c1, INA226_ADDR , reg_addr, I2C_MEMADD_SIZE_8BIT, data, 2, 100) == HAL_OK) {
//        // 将高位字节和低位字节拼接 (高位在前)
//        return (uint16_t)((data[0] << 8) | data[1]);
//    }
//    return 0;
//}

//float INA226_GetBusVoltage(void) {
//    uint16_t raw_val = INA226_ReadReg(0x02);
//	
//    // 原始值 * 1.25 / 1000 得到单位为 V 的电压
//    return (float)raw_val * 0.00125f;
//}

//// 这里的 CURRENT_LSB 是你在初始化计算中定义的 (例如 0.0001 表示 0.1mA)
//float CURRENT_LSB = 0.0001f; 

//float INA226_GetCurrent(void) {
//    int16_t raw_val = (int16_t)INA226_ReadReg(0x04); // 电流是带符号的
//    // 电流 = 原始值 * Current_LSB
//    return (float)raw_val * CURRENT_LSB; // 单位：A
//}

//float INA226_GetPower(void) {
//    uint16_t raw_val = INA226_ReadReg(0x03);
//    // 功率 = 原始值 * (Current_LSB * 25)
//    return (float)raw_val * (CURRENT_LSB * 25.0f); // 单位：W
//}

//void INA226_Init(void) {
//    // 1. 配置寄存器: 0x4527 (16次平均, 1.1ms采样, 连续测量)
//    INA226_WriteReg(0x00, 0x4127);
//    
//    // 2. 校准寄存器: 必须写入！
//    // 假设 Rshunt = 0.1 Ohm, 期望 Current_LSB = 0.1mA (0.0001)
//    // CAL = 0.00512 / (0.0001 * 0.1) = 512 = 0x0200
//    INA226_WriteReg(0x05, 0x2155); 
//}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
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
  for(int i = 0; i < 3; i++) {
    INA226_InitDevice(&sensors[i]);
  }

  /* power_ctrl: 受控上电时序 */
  printf("[PWR] init: all EN pins LOW\r\n");
  HAL_Delay(100);

  printf("[PWR] step1: EN_TPS43060 HIGH (PB13=1) 12->24V\r\n");
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
  printf("[PWR] waiting 500ms for 24V bus...\r\n");
  HAL_Delay(500);

  printf("[PWR] step2: EN_24TO12 HIGH (PB14=1) 24->12V\r\n");
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
  printf("[PWR] waiting 200ms for 12V_FAN...\r\n");
  HAL_Delay(200);

  printf("[PWR] step3: CHARGE_EN HIGH (PB12=1) battery charger\r\n");
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
  HAL_Delay(50);

  printf("[PWR] step4: PUMP_EN HIGH (PB15=1) pump output\r\n");
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
  HAL_Delay(50);

  printf("[PWR] power init done\r\n");

  /* 开机提示：大疆开机音 + LED */
  Play_DJI_Startup();

  /* actuator_ctrl: 风扇开环控制 */
  printf("[FAN] FAN_VCC_CTRL HIGH (PC6=1)\r\n");
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_SET);
  HAL_Delay(100);

  printf("[FAN] PWM start 50%% on PC8 (TIM3_CH3, 20kHz)\r\n");
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  printf("[FAN] open-loop running\r\n");

  /* YSJ compressor test: DIR=default, BRAKE release, PWM 30% */
  printf("[YSJ] DIR=default (PB3=LOW)\r\n");
  printf("[YSJ] BRAKE release (PD7=HIGH)\r\n");
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);
  HAL_Delay(50);
  printf("[YSJ] PWM 30%% on PB5 (TIM3_CH2, 20kHz)\r\n");
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 15);  /* 30% of ARR=49: 15/50 */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  printf("[YSJ] compressor test running\r\n");

  /* SC_COUNT input capture on PB4 (TIM3_CH1) */
  HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);
  __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
  printf("[YSJ] SC_COUNT IC started\r\n");

  /* FAN_FB input capture on PC7 (TIM8_CH2) */
  HAL_TIM_IC_Start_IT(&htim8, TIM_CHANNEL_2);
  __HAL_TIM_ENABLE_IT(&htim8, TIM_IT_UPDATE);
  printf("[FAN] FB IC started\r\n");

  /* sensor_acq: 启动 ADC1 DMA 连续扫描 */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, 8);
  printf("[NTC] ADC1 DMA started\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    for (int i = 0; i < 3; i++) {
        INA226_UpdateData(&sensors[i]);
    }
    for (int i = 0; i < 3; i++) {
        printf("v%d = %.3fV, i%d = %.3fA, p%d = %.3fW\r\n",
               i + 1, sensors[i].v,
               i + 1, sensors[i].i,
               i + 1, sensors[i].p);
    }
    uint32_t fan_rpm = fan_freq_hz * 20UL;  /* RPM = Hz * 60 / pole_pairs(3) = Hz * 20 */
    uint32_t sc_rpm  = sc_freq_hz  * 10UL;  /* RPM = Hz * 60 / pole_pairs(6) = Hz * 10 */
    printf("[FAN] %lu Hz  %lu RPM  [YSJ] %lu Hz  %lu RPM\r\n",
           fan_freq_hz, fan_rpm, sc_freq_hz, sc_rpm);
    printf("[NTC] 2-1=%4d 2-2=%4d 2-3=%4d 2-4=%4d\r\n",
           adc_buf[3], adc_buf[0], adc_buf[1], adc_buf[2]);
    printf("[NTC] 1-1=%4d 1-2=%4d 1-3=%4d 1-4=%4d\r\n",
           adc_buf[7], adc_buf[4], adc_buf[5], adc_buf[6]);
    HAL_Delay(500);
    iii++;
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* Input capture callback — SC_COUNT (TIM3_CH1) and FAN_FB (TIM8_CH2) */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        if (ic_state == 0) {
            ic_val1    = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
            ic_overflow = 0;
            ic_state   = 1;
        } else {
            ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
            uint32_t ticks;
            if (ic_val2 >= ic_val1) {
                ticks = ic_overflow * 50U + (ic_val2 - ic_val1);
            } else {
                ticks = (ic_overflow + 1U) * 50U - (ic_val1 - ic_val2);
            }
            if (ticks > 0U) {
                sc_freq_hz = 1000000UL / ticks;
            }
            ic_state = 0;
        }
    }
    else if (htim->Instance == TIM8 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        if (fan_ic_state == 0) {
            fan_ic_val1    = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
            fan_ic_overflow = 0;
            fan_ic_state   = 1;
        } else {
            fan_ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
            uint32_t ticks;
            if (fan_ic_val2 >= fan_ic_val1) {
                ticks = fan_ic_overflow * 65536U + (fan_ic_val2 - fan_ic_val1);
            } else {
                ticks = (fan_ic_overflow + 1U) * 65536U - (fan_ic_val1 - fan_ic_val2);
            }
            if (ticks > 0U) {
                uint32_t f = 1000000UL / ticks;
                /* FG valid range: 1~1000 Hz, reject PWM crosstalk (>1000Hz) */
                fan_freq_hz = (f >= 1U && f <= 1000U) ? f : 0U;
            }
            fan_ic_state = 0;
        }
    }
}

/* Overflow callbacks — count overflows between captures, handle timeout */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        if (ic_state == 1) {
            ic_overflow++;
            if (ic_overflow > 1000U) {   /* ~50ms timeout, signal absent */
                sc_freq_hz = 0;
                ic_state   = 0;
            }
        }
    }
    else if (htim->Instance == TIM8) {
        if (fan_ic_state == 1) {
            fan_ic_overflow++;
            if (fan_ic_overflow > 100U) {  /* ~6.5s timeout */
                fan_freq_hz = 0;
                fan_ic_state = 0;
            }
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
