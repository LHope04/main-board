/**
 * @file    Core/Src/usart.c
 * @brief   USART configuration for V6 main board.
 *
 * USART role table:
 *   USART1 PA9/PA10  (AF7) — 有线遥控 RemoteCtrl (stub, 阶段 10)
 *   USART2 PA2/PA3   (AF7) — BC260Y 物联网 IotCtrl (stub, 阶段 10), 经 R15/R18 100Ω 串联
 *   USART3 PD8/PD9   (AF7) — 综合采样板 SamplerComm (阶段 7)
 *   USART6 PC6/PC7   (AF8) — ESP32-C3 BLE EspComm (阶段 8), 经 R6/R8 100Ω 串联
 *
 * NVIC 优先级: USART3=1,0; USART6=1,1; USART2=2,0; USART1=2,1
 * 实际 NVIC 使能放在各模块自己的 Init 内，避免 RxISR 未绑定时的野中断。
 */
#include "usart.h"

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
UART_HandleTypeDef huart6;

static void uart_init_common(UART_HandleTypeDef *h, USART_TypeDef *inst)
{
    h->Instance          = inst;
    h->Init.BaudRate     = 115200;
    h->Init.WordLength   = UART_WORDLENGTH_8B;
    h->Init.StopBits     = UART_STOPBITS_1;
    h->Init.Parity       = UART_PARITY_NONE;
    h->Init.Mode         = UART_MODE_TX_RX;
    h->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(h) != HAL_OK) {
        Error_Handler();
    }
}

void MX_USART1_UART_Init(void) { uart_init_common(&huart1, USART1); }
void MX_USART2_UART_Init(void) { uart_init_common(&huart2, USART2); }
void MX_USART3_UART_Init(void) { uart_init_common(&huart3, USART3); }
void MX_USART6_UART_Init(void) { uart_init_common(&huart6, USART6); }

void HAL_UART_MspInit(UART_HandleTypeDef *uartHandle)
{
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_AF_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    if (uartHandle->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
        gi.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gi);
    }
    else if (uartHandle->Instance == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
        gi.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &gi);
    }
    else if (uartHandle->Instance == USART3) {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
        gi.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOD, &gi);
    }
    else if (uartHandle->Instance == USART6) {
        __HAL_RCC_USART6_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        gi.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
        gi.Alternate = GPIO_AF8_USART6;
        HAL_GPIO_Init(GPIOC, &gi);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *uartHandle)
{
    if (uartHandle->Instance == USART1) {
        __HAL_RCC_USART1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
    }
    else if (uartHandle->Instance == USART2) {
        __HAL_RCC_USART2_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
    }
    else if (uartHandle->Instance == USART3) {
        __HAL_RCC_USART3_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOD, GPIO_PIN_8 | GPIO_PIN_9);
    }
    else if (uartHandle->Instance == USART6) {
        __HAL_RCC_USART6_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);
    }
}
