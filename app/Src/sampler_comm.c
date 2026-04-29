/**
 * @file    app/Src/sampler_comm.c
 * @brief   USART3 protocol bridge to the sampler board (NTC raw ADC stream).
 *
 * State machine and ring-buffer layout are modelled after esp_comm.c — same
 * AA/CMD/LEN/PAYLOAD/XOR framing, just no OTA dispatch table on top. Frames
 * land in a slot ring; SamplerComm_Poll() dispatches in main-loop context
 * (so SensorAcq_OnNtcFrame() can do work that's unsafe in ISR).
 *
 * RX is interrupt-driven (RXNE). TX is polled HAL_UART_Transmit (heartbeat
 * is short and infrequent — 5 bytes per 500ms — so blocking is fine).
 */
#include "sampler_comm.h"
#include "sensor_acq.h"
#include <string.h>

#define MAX_PAYLOAD     32U
#define FRAME_SLOTS     4U   /* power of 2 */

typedef enum {
    RX_WAIT_HEADER,
    RX_GOT_HEADER,
    RX_GOT_CMD,
    RX_GOT_LEN,
    RX_PAYLOAD,
    RX_CHECK_XOR,
} rx_state_t;

static UART_HandleTypeDef *s_huart;

/* === Debug counters (SWD/watch) === */
volatile uint32_t g_sampler_hb_tx_cnt    = 0;   /* 心跳发送次数 */
volatile uint32_t g_sampler_frames_rx    = 0;   /* 收到合法帧总数 */
volatile uint32_t g_sampler_xor_errors   = 0;   /* XOR 校验失败次数 */
volatile uint32_t g_sampler_isr_cnt      = 0;   /* RxISR 进入次数 */

/* ISR-side scratch (single in-flight frame) */
static volatile rx_state_t s_rx_state  = RX_WAIT_HEADER;
static volatile uint8_t    s_rx_cmd    = 0;
static volatile uint8_t    s_rx_len    = 0;
static volatile uint8_t    s_rx_idx    = 0;
static volatile uint8_t    s_rx_buf[MAX_PAYLOAD];
static volatile uint8_t    s_rx_xor    = 0;

/* Slot ring (ISR producer, Poll consumer) */
static volatile uint8_t    s_slot_cmd[FRAME_SLOTS];
static volatile uint8_t    s_slot_len[FRAME_SLOTS];
static volatile uint8_t    s_slot_payload[FRAME_SLOTS][MAX_PAYLOAD];
static volatile uint32_t   s_slot_wr   = 0;
static volatile uint32_t   s_slot_rd   = 0;

void SamplerComm_Init(UART_HandleTypeDef *huart)
{
    s_huart    = huart;
    s_rx_state = RX_WAIT_HEADER;
    s_rx_idx   = 0;
    s_rx_xor   = 0;
    s_slot_wr  = 0;
    s_slot_rd  = 0;
    memset((void *)s_slot_cmd, 0, sizeof(s_slot_cmd));
    memset((void *)s_slot_len, 0, sizeof(s_slot_len));

    if (!s_huart) return;

    __HAL_UART_CLEAR_FLAG(s_huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE);
    (void)s_huart->Instance->DR;

    HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_RXNE);
}

static void slot_commit(void)
{
    uint8_t slot = (uint8_t)(s_slot_wr & (FRAME_SLOTS - 1U));
    s_slot_cmd[slot] = s_rx_cmd;
    s_slot_len[slot] = s_rx_len;
    if (s_rx_len > 0) {
        for (uint8_t i = 0; i < s_rx_len; i++) {
            s_slot_payload[slot][i] = s_rx_buf[i];
        }
    }
    s_slot_wr++;
}

void SamplerComm_RxISR(void)
{
    g_sampler_isr_cnt++;
    if (!s_huart) return;
    USART_TypeDef *reg = s_huart->Instance;
    uint32_t sr = reg->SR;

    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        (void)reg->DR;
        return;
    }
    if (!(sr & USART_SR_RXNE)) return;

    uint8_t b = (uint8_t)(reg->DR & 0xFFU);

    switch (s_rx_state) {
        case RX_WAIT_HEADER:
            if (b == SAMPLER_FRAME_HEADER) {
                s_rx_state = RX_GOT_HEADER;
                s_rx_xor   = 0;
            }
            break;
        case RX_GOT_HEADER:
            s_rx_cmd   = b;
            s_rx_xor  ^= b;
            s_rx_state = RX_GOT_CMD;
            break;
        case RX_GOT_CMD:
            s_rx_len   = b;
            s_rx_xor  ^= b;
            s_rx_idx   = 0;
            if (s_rx_len > MAX_PAYLOAD) {
                s_rx_state = RX_WAIT_HEADER;     /* invalid len, drop */
                break;
            }
            s_rx_state = (s_rx_len == 0) ? RX_CHECK_XOR : RX_PAYLOAD;
            break;
        case RX_PAYLOAD:
            s_rx_buf[s_rx_idx++] = b;
            s_rx_xor            ^= b;
            if (s_rx_idx >= s_rx_len) {
                s_rx_state = RX_CHECK_XOR;
            }
            break;
        case RX_CHECK_XOR:
            if (b == s_rx_xor) {
                slot_commit();
                g_sampler_frames_rx++;
            } else {
                g_sampler_xor_errors++;
            }
            s_rx_state = RX_WAIT_HEADER;
            break;
    }
}

void SamplerComm_Poll(void)
{
    while (s_slot_rd != s_slot_wr) {
        uint8_t slot = (uint8_t)(s_slot_rd & (FRAME_SLOTS - 1U));
        uint8_t cmd  = s_slot_cmd[slot];
        uint8_t len  = s_slot_len[slot];
        const uint8_t *payload = (const uint8_t *)s_slot_payload[slot];

        switch (cmd) {
            case SAMPLER_CMD_NTC:
                if (len == 16U) SensorAcq_OnNtcFrame(payload);
                break;
            case SAMPLER_CMD_SELFTEST:
                SensorAcq_OnSelftestFrame(payload, len);
                break;
            default:
                break;
        }
        s_slot_rd++;
    }
}

void SamplerComm_SendHeartbeat(uint8_t seq)
{
    if (!s_huart) return;
    uint8_t f[5] = {
        SAMPLER_FRAME_HEADER,
        SAMPLER_CMD_HEARTBEAT,
        0x01U,
        seq,
        (uint8_t)(SAMPLER_CMD_HEARTBEAT ^ 0x01U ^ seq),
    };
    (void)HAL_UART_Transmit(s_huart, f, sizeof(f), 20);
    g_sampler_hb_tx_cnt++;
}
