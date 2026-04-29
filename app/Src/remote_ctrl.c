/**
 * @file    app/Src/remote_ctrl.c
 *
 * STUB module — see remote_ctrl.h for留空 contract.
 * Stage-1 baseline: enable USART1 RXNE interrupt and silently drop bytes.
 */
#include "remote_ctrl.h"

static UART_HandleTypeDef *s_huart;

/* Optional debug counter — visible from SWD: */
volatile uint32_t g_remote_rx_drops = 0;
volatile uint32_t g_remote_poll_cnt = 0;

void RemoteCtrl_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    if (!s_huart) return;

    __HAL_UART_CLEAR_FLAG(s_huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE);
    (void)s_huart->Instance->DR;

    HAL_NVIC_SetPriority(USART1_IRQn, 2, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_RXNE);
}

void RemoteCtrl_RxISR(void)
{
    if (!s_huart) return;
    USART_TypeDef *reg = s_huart->Instance;
    uint32_t sr = reg->SR;
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        (void)reg->DR;
        return;
    }
    if (sr & USART_SR_RXNE) {
        (void)reg->DR;       /* TODO(stub): feed framing state machine */
        g_remote_rx_drops++;
    }
}

void RemoteCtrl_Poll(void)
{
    /* TODO(stub): drain frames + dispatch to business layer */
    g_remote_poll_cnt++;
}
