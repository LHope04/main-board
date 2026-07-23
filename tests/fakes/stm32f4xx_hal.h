#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U,
    HAL_BUSY = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
} USART_TypeDef;

typedef struct {
    USART_TypeDef *Instance;
} UART_HandleTypeDef;

#define USART_SR_PE   (1U << 0)
#define USART_SR_FE   (1U << 1)
#define USART_SR_NE   (1U << 2)
#define USART_SR_ORE  (1U << 3)
#define USART_SR_RXNE (1U << 5)

#define UART_FLAG_ORE USART_SR_ORE
#define UART_FLAG_FE  USART_SR_FE
#define UART_FLAG_NE  USART_SR_NE
#define UART_FLAG_PE  USART_SR_PE
#define UART_IT_RXNE  USART_SR_RXNE

#define USART2_IRQn 38
#define USART3_IRQn 39

#define __HAL_UART_CLEAR_FLAG(huart, flag) do { (void)(huart); (void)(flag); } while (0)
#define __HAL_UART_ENABLE_IT(huart, it)    do { (void)(huart); (void)(it); } while (0)

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data,
                                    uint16_t len, uint32_t timeout);
void HAL_NVIC_SetPriority(int irq, int preempt_priority, int sub_priority);
void HAL_NVIC_EnableIRQ(int irq);

#endif
