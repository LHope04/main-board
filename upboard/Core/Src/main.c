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
    
    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, 10);
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
 * 图1 (I2C3): Rshunt = 0.006 Ohm. 设 LSB=1mA(0.001), CAL = 0.00512/(0.001*0.006) = 853 (0x0355)
 * 图2 (I2C1): Rshunt = 0.1 Ohm.   设 LSB=0.1mA(0.0001), CAL = 0.00512/(0.0001*0.1) = 512 (0x0200)
 */
INA226_Device sensors[6] = {
    // I2C1 (Rshunt = 100mr, LSB = 0.1mA)
    {&hi2c1, 0x80, 0x0200, 0.0001f}, // v1: 水泵
    {&hi2c1, 0x82, 0x0200, 0.0001f}, // v2: 风扇
    
    // I2C2 (Rshunt = 6mr, LSB = 1mA) -> 校准值修正为 0x0355
    {&hi2c2, 0x88, 0x0355, 0.0012f},  // v3: 24v输入
    {&hi2c2, 0x8A, 0x0355, 0.0012f},  // v4: 锂电池
    
    // I2C3 (Rshunt = 6mr, LSB = 1mA)
    {&hi2c3, 0x88, 0x0355, 0.0012f},  // v5: 输入总功率
    {&hi2c3, 0x82, 0x0355, 0.0012f}   // v6: 压缩机功率
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
  MX_I2C2_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
// 1. 初始化模拟IIC引脚
//  INA226_SoftI2C_Init(); 
 // INA226_Init();
 for(int i = 0; i < 6; i++) {
    INA226_InitDevice(&sensors[i]);
}
  // 2. 初始化两个INA226芯片（配置寄存器和校准寄存器）
 // INA226_Init(DEVICE1_ADDR);
  //HAL_Delay(10); // 稍微延时确保稳定
  //INA226_Init(DEVICE2_ADDR);
// 强制解锁 I2C 总线

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		
		
for (int i = 0; i < 6; i++) {
        // 1. 更新数据
        INA226_UpdateData(&sensors[i]);
        
    }
//		INA226_UpdateData(&sensors[2]);
for (int i = 0; i < 6; i++) {
        // 1. 更新数据

        
        // 2. 格式化输出到 USART3
        printf("v%d = %.3fV, i%d = %.3fA, p%d = %.3fW\r\n", 
               i + 1, sensors[i].v, 
               i + 1, sensors[i].i, 
               i + 1, sensors[i].p);
	HAL_Delay(10);
    }
//		uint8_t ab = 1;
//HAL_UART_Transmit(&huart3, (uint8_t *)ab, 1, 10);
HAL_Delay(10);
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
