/**
 * @file    app/Src/iot_ctrl.c
 *
 * STUB module — see iot_ctrl.h for 留空 contract.
 * Stage-1 baseline: enable USART2 RXNE interrupt and silently drop bytes.
 */
#include "iot_ctrl.h"

static UART_HandleTypeDef *s_huart;

volatile uint32_t g_iot_rx_drops = 0;
volatile uint32_t g_iot_poll_cnt = 0;

void IotCtrl_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    if (!s_huart) return;

    __HAL_UART_CLEAR_FLAG(s_huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE);
    (void)s_huart->Instance->DR;

    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_RXNE);
}

void IotCtrl_RxISR(void)
{
    if (!s_huart) return;
    USART_TypeDef *reg = s_huart->Instance;
    uint32_t sr = reg->SR;
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        (void)reg->DR;
        return;
    }
    if (sr & USART_SR_RXNE) {
        (void)reg->DR;       /* TODO(stub): BC260Y AT URC dispatch */
        g_iot_rx_drops++;
    }
}

void IotCtrl_Poll(void)
{
    /* TODO(stub): AT command queue + URC dispatcher */
    g_iot_poll_cnt++;
}
