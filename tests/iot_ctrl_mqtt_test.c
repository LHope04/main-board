#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iot_ctrl.h"

static USART_TypeDef fake_usart2;
static UART_HandleTypeDef fake_huart2 = { &fake_usart2 };
static uint32_t fake_tick;
static char tx_log[4096];
static size_t tx_len;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data,
                                    uint16_t len, uint32_t timeout)
{
    (void)huart;
    (void)timeout;
    assert(tx_len + len < sizeof(tx_log));
    memcpy(&tx_log[tx_len], data, len);
    tx_len += len;
    tx_log[tx_len] = '\0';
    return HAL_OK;
}

void HAL_NVIC_SetPriority(int irq, int preempt_priority, int sub_priority)
{
    (void)irq;
    (void)preempt_priority;
    (void)sub_priority;
}

void HAL_NVIC_EnableIRQ(int irq)
{
    (void)irq;
}

static void feed_byte(uint8_t b)
{
    fake_usart2.SR = USART_SR_RXNE;
    fake_usart2.DR = b;
    IotCtrl_RxISR();
}

static void feed_text(const char *text)
{
    while (*text) {
        feed_byte((uint8_t)*text++);
    }
}

static void poll_ms(uint32_t step_ms)
{
    fake_tick += step_ms;
    IotCtrl_Poll();
}

static void expect_tx_contains(const char *needle)
{
    if (strstr(tx_log, needle) == NULL) {
        fprintf(stderr, "missing tx fragment: %s\nTX log:\n%s\n", needle, tx_log);
        assert(0);
    }
}

int main(void)
{
    IotCtrl_Init(&fake_huart2);

    IotCtrl_TelemetrySnapshot snap = {0};
    for (int i = 0; i < 8; i++) {
        snap.ntc_raw[i] = (uint16_t)(1800 + i);
    }
    snap.bat24_mv = 24200;
    snap.bat24_ma = 320;
    snap.v12_mv = 12600;
    snap.boost_on = 1;
    snap.load_on = 1;
    snap.pump_on = 1;
    snap.pump_duty_pct = 45;
    snap.gps_fix = 1;
    snap.gps_lat_e6 = 22300001;
    snap.gps_lon_e6 = 114100002;
    snap.gps_speed_milli_knots = 300;
    IotCtrl_SetTelemetrySnapshot(&snap);

    poll_ms(3000);
    expect_tx_contains("ATE0\r\n");
    feed_text("\r\nOK\r\n");

    poll_ms(10);
    expect_tx_contains("AT\r\n");
    feed_text("\r\nOK\r\n");

    poll_ms(10);
    feed_text("\r\n+CPIN: READY\r\n\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\n+CSQ: 22,99\r\n\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\n+CEREG: 0,1\r\n\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\n+CGATT: 1\r\n\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\nOK\r\n");
    poll_ms(10);
    feed_text("\r\nERROR\r\n");
    poll_ms(10);
    feed_text("\r\nERROR\r\n");

    poll_ms(10);
    expect_tx_contains("AT+QMTOPEN=0,\"38.76.206.42\",1883\r\n");
    feed_text("\r\nOK\r\n\r\n+QMTOPEN: 0,0\r\n");

    poll_ms(10);
    expect_tx_contains("AT+QMTCONN=0,");
    feed_text("\r\nOK\r\n\r\n+QMTCONN: 0,0,0\r\n");
    poll_ms(10);
    expect_tx_contains("AT+QMTSUB=0,1,\"upboard/UPB-TEST-001/command/pump\",1\r\n");
    feed_text("\r\nOK\r\n\r\n+QMTSUB: 0,1,0,1\r\n");
    poll_ms(10);
    assert(g_iot_mqtt_connected == 1U);
    assert(g_iot_sub_ok_count == 1U);

    feed_text("\r\n+QMTRECV: 0,0,\"upboard/UPB-TEST-001/command/pump\",{\"duty_pct\":65}\r\n");
    uint8_t pump_duty = 0U;
    assert(IotCtrl_TakePumpCommand(&pump_duty) == 1);
    assert(pump_duty == 65U);
    assert(IotCtrl_TakePumpCommand(&pump_duty) == 0);

    feed_text("\r\n+QMTRECV: 0,0,\"upboard/UPB-TEST-001/command/pump\",{\"duty_pct\":101}\r\n");
    assert(g_iot_pump_cmd_invalid_count == 1U);
    assert(IotCtrl_TakePumpCommand(&pump_duty) == 0);

    poll_ms(5000);
    expect_tx_contains("AT+QMTPUBEX=0,0,0,0,\"upboard/");
    expect_tx_contains("/telemetry\",");
    feed_text("\r\n> ");
    poll_ms(10);
    expect_tx_contains("\"gps\":{\"fix\":true,\"lat\":22.300001,\"lon\":114.100002,\"speed_kt\":0.300}");
    expect_tx_contains("\"sampler\":{\"stale\":false,\"ntc_raw\":[1800,1801,1802,1803,1804,1805,1806,1807]}");
    expect_tx_contains("\"pump\":true,\"pump_duty_pct\":45");
    assert(strchr(tx_log, 0x1A) == NULL);
    feed_text("\r\n+QMTPUBEX: 0,0,0\r\n");
    poll_ms(10);
    assert(g_iot_pub_ok_count == 1U);

    puts("iot_ctrl MQTT state test passed");
    return 0;
}
