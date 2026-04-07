#include "esp_comm.h"
#include <string.h>

/* ---- private types ---- */
typedef enum {
    RX_WAIT_HEADER,
    RX_GOT_CMD,
    RX_GOT_LEN,
    RX_PAYLOAD,
    RX_CHECK_XOR
} RxState;

/* ---- private variables ---- */
static UART_HandleTypeDef *s_huart;

/* ISR receive state machine */
static volatile RxState  s_rx_state;
static volatile uint8_t  s_rx_cmd;
static volatile uint8_t  s_rx_len;
static volatile uint8_t  s_rx_idx;
static volatile uint8_t  s_rx_xor;
static volatile uint8_t  s_rx_buf[ESP_MAX_PAYLOAD];

/* Complete frame buffer (double-buffer: ISR writes, Poll reads) */
#define FRAME_SLOTS  4
static volatile uint8_t  s_frame_cmd[FRAME_SLOTS];
static volatile uint8_t  s_frame_len[FRAME_SLOTS];
static volatile uint8_t  s_frame_payload[FRAME_SLOTS][ESP_MAX_PAYLOAD];
static volatile uint8_t  s_frame_wr;   /* written by ISR */
static volatile uint8_t  s_frame_rd;   /* read by Poll */

/* Application data */
 EspComm_GearCmd s_gear_cmd;

/* Debug: raw byte ring buffer — view in Keil Memory at &s_dbg_raw */
#define DBG_RAW_SIZE  64
volatile uint8_t  s_dbg_raw[DBG_RAW_SIZE];
volatile uint8_t  s_dbg_wr;
volatile uint32_t s_dbg_cnt;  /* total bytes received */

/* ---- private helpers ---- */

static void send_raw(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(s_huart, (uint8_t *)data, len, 20);
}

static void send_ping_ack(void)
{
    const uint8_t frame[] = { 0xAA, 0x81, 0x00, 0x81 };
    send_raw(frame, 4);
}

static void handle_set_gear(const volatile uint8_t *payload, uint8_t len)
{
    if (len < 3) return;
    s_gear_cmd.gear    = (int16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
    s_gear_cmd.on      = payload[2];
    s_gear_cmd.updated = 1;
}

/* ---- public API ---- */

void EspComm_Init(UART_HandleTypeDef *huart)
{
    s_huart    = huart;
    s_rx_state = RX_WAIT_HEADER;
    s_frame_wr = 0;
    s_frame_rd = 0;
    memset(&s_gear_cmd, 0, sizeof(s_gear_cmd));

    /* Clear any pending flags before enabling interrupt */
    __HAL_UART_CLEAR_FLAG(s_huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE);
    (void)s_huart->Instance->DR;  /* dummy read to clear RXNE/ORE */

    /* Enable NVIC + RXNE interrupt */
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(s_huart, UART_IT_RXNE);
}

void EspComm_RxISR(void)
{
    if (!s_huart) return;
    USART_TypeDef *reg = s_huart->Instance;
    uint32_t sr = reg->SR;

    /* Clear error flags (ORE/FE/NE/PE) — read SR then DR */
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        (void)reg->DR;   /* reading DR clears the error flags */
        return;
    }

    /* Check RXNE flag */
    if (!(sr & USART_SR_RXNE))
        return;

    uint8_t byte = (uint8_t)(reg->DR & 0xFF);

    /* Log raw byte for debug */
    s_dbg_raw[s_dbg_wr] = byte;
    s_dbg_wr = (s_dbg_wr + 1) & (DBG_RAW_SIZE - 1);
    s_dbg_cnt++;

    switch (s_rx_state) {
    case RX_WAIT_HEADER:
        if (byte == ESP_FRAME_HEADER)
            s_rx_state = RX_GOT_CMD;
        break;

    case RX_GOT_CMD:
        s_rx_cmd   = byte;
        s_rx_xor   = byte;
        s_rx_state = RX_GOT_LEN;
        break;

    case RX_GOT_LEN:
        s_rx_len = byte;
        s_rx_xor ^= byte;
        s_rx_idx = 0;
        if (s_rx_len > ESP_MAX_PAYLOAD) {
            s_rx_state = RX_WAIT_HEADER;  /* invalid, reset */
        } else if (s_rx_len == 0) {
            s_rx_state = RX_CHECK_XOR;
        } else {
            s_rx_state = RX_PAYLOAD;
        }
        break;

    case RX_PAYLOAD:
        s_rx_buf[s_rx_idx] = byte;
        s_rx_xor ^= byte;
        s_rx_idx++;
        if (s_rx_idx >= s_rx_len)
            s_rx_state = RX_CHECK_XOR;
        break;

    case RX_CHECK_XOR:
        if (byte == s_rx_xor) {
            /* Valid frame — push into ring buffer */
            uint8_t wr = s_frame_wr;
            uint8_t slot = wr & (FRAME_SLOTS - 1);
            s_frame_cmd[slot] = s_rx_cmd;
            s_frame_len[slot] = s_rx_len;
            for (uint8_t i = 0; i < s_rx_len; i++)
                s_frame_payload[slot][i] = s_rx_buf[i];
            s_frame_wr = wr + 1;
        }
        s_rx_state = RX_WAIT_HEADER;
        break;
    }
}

void EspComm_Poll(void)
{
    while (s_frame_rd != s_frame_wr) {
        uint8_t slot = s_frame_rd & (FRAME_SLOTS - 1);
        uint8_t cmd  = s_frame_cmd[slot];
        uint8_t len  = s_frame_len[slot];

        switch (cmd) {
        case ESP_CMD_PING:
            send_ping_ack();
            break;
        case ESP_CMD_SET_GEAR:
            handle_set_gear(s_frame_payload[slot], len);
            break;
        default:
            break;
        }
        s_frame_rd++;
    }
}

void EspComm_SendStatus(const EspComm_Status *st)
{
    uint8_t frame[4 + 8];   /* header + cmd + len + 8 payload + xor */
    frame[0] = ESP_FRAME_HEADER;
    frame[1] = ESP_CMD_STATUS;
    frame[2] = 8;  /* payload length */

    /* payload — little-endian */
    frame[3] = (uint8_t)(st->water_temp_x10 & 0xFF);
    frame[4] = (uint8_t)((st->water_temp_x10 >> 8) & 0xFF);
    frame[5] = st->battery_pct;
    frame[6] = (uint8_t)(st->total_power_w & 0xFF);
    frame[7] = (uint8_t)((st->total_power_w >> 8) & 0xFF);
    frame[8] = st->error_flags;
    frame[9] = (uint8_t)(st->ambient_temp_x10 & 0xFF);
    frame[10] = (uint8_t)((st->ambient_temp_x10 >> 8) & 0xFF);

    /* XOR = CMD ^ LEN ^ payload bytes */
    uint8_t xor = frame[1] ^ frame[2];
    for (uint8_t i = 0; i < 8; i++)
        xor ^= frame[3 + i];
    frame[11] = xor;

    send_raw(frame, 12);
}

EspComm_GearCmd *EspComm_GetGearCmd(void)
{
    return &s_gear_cmd;
}
