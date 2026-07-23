/**
 * @file    app/Src/iot_ctrl.c
 *
 * Quectel EC801E MQTT uplink state machine.
 */
#include "iot_ctrl.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define IOT_RX_RING_SIZE        128U
#define IOT_LINE_SIZE            96U
#define IOT_CMD_SIZE            160U
#define IOT_PAYLOAD_SIZE        384U

#define IOT_BOOT_DELAY_MS      3000U
#define IOT_CMD_TIMEOUT_MS     3000U
#define IOT_SHORT_TIMEOUT_MS   1000U
#define IOT_OPEN_TIMEOUT_MS   75000U
#define IOT_CONN_TIMEOUT_MS   15000U
#define IOT_PUB_TIMEOUT_MS    20000U
#define IOT_PUB_INTERVAL_MS    5000U
#define IOT_BACKOFF_MIN_MS     5000U
#define IOT_BACKOFF_MAX_MS    60000U
#define IOT_AT_RETRY_MS        1000U

#define IOT_MQTT_HOST          "38.76.206.42"
#define IOT_MQTT_PORT          1883U
#define IOT_MQTT_USERNAME      "upboard_device"
#define IOT_MQTT_PASSWORD      "mriTbXbm0MsMTF7irNergKE8"
#define IOT_MQTT_TOPIC_PREFIX  "upboard"

enum {
    IOT_ST_DISABLED = 0,
    IOT_ST_BOOT_WAIT = 1,
    IOT_ST_WAIT_ATE0 = 10,
    IOT_ST_WAIT_AT,
    IOT_ST_WAIT_CPIN,
    IOT_ST_WAIT_CSQ,
    IOT_ST_WAIT_CEREG,
    IOT_ST_WAIT_CGATT,
    IOT_ST_WAIT_QICSGP,
    IOT_ST_WAIT_QIACT,
    IOT_ST_WAIT_QMTCFG_VERSION,
    IOT_ST_WAIT_QMTCFG_PDPCID,
    IOT_ST_WAIT_QMTDISC,
    IOT_ST_WAIT_QMTCLOSE,
    IOT_ST_WAIT_QMTOPEN,
    IOT_ST_WAIT_QMTCONN,
    IOT_ST_CONNECTED = 40,
    IOT_ST_WAIT_PUB_PROMPT,
    IOT_ST_WAIT_PUB_RESULT,
    IOT_ST_BACKOFF = 90,
};

enum {
    IOT_ERR_NONE = 0,
    IOT_ERR_UART_TX = 1,
    IOT_ERR_ERROR_LINE = 2,
    IOT_ERR_TIMEOUT = 3,
    IOT_ERR_QMTOPEN = 4,
    IOT_ERR_QMTCONN = 5,
    IOT_ERR_QMTPUBEX = 6,
    IOT_ERR_PAYLOAD_TOO_LONG = 7,
    IOT_ERR_QMTSTAT = 8,
};

static UART_HandleTypeDef *s_huart;
static char s_line_buf[IOT_LINE_SIZE];
static uint32_t s_line_len;
static uint32_t s_last_at_ms;

static uint32_t s_state_started_ms;
static uint32_t s_deadline_ms;
static uint32_t s_backoff_ms = IOT_BACKOFF_MIN_MS;
static uint32_t s_backoff_until_ms;
static uint32_t s_last_pub_ms;
static uint32_t s_wait_ok_seq;
static uint32_t s_wait_error_seq;
static uint32_t s_wait_prompt_seq;
static uint32_t s_wait_qmtopen_seq;
static uint32_t s_wait_qmtconn_seq;
static uint32_t s_wait_qmtpub_seq;
static uint32_t s_wait_qmtstat_seq;
static uint32_t s_seen_ok_seq;
static uint32_t s_seen_error_seq;
static uint32_t s_qmtopen_seq;
static uint32_t s_qmtopen_result = 0xFFFFFFFFU;
static uint32_t s_qmtconn_seq;
static uint32_t s_qmtconn_result = 0xFFFFFFFFU;
static uint32_t s_qmtpub_seq;
static uint32_t s_qmtpub_result = 0xFFFFFFFFU;
static uint32_t s_qmtstat_seq;
static IotCtrl_TelemetrySnapshot s_snapshot;

volatile uint32_t g_iot_poll_cnt = 0;
volatile uint32_t g_iot_enable = 1;
volatile uint32_t g_iot_state = IOT_ST_DISABLED;
volatile uint32_t g_iot_last_error = IOT_ERR_NONE;
volatile uint32_t g_iot_mqtt_connected = 0;
volatile uint32_t g_iot_reconnect_count = 0;
volatile uint32_t g_iot_pub_ok_count = 0;
volatile uint32_t g_iot_pub_fail_count = 0;
volatile uint32_t g_iot_seq = 0;
volatile uint32_t g_iot_last_payload_len = 0;
volatile uint32_t g_iot_last_csq = 0xFFFFFFFFU;
volatile uint32_t g_iot_prompt_count = 0;
volatile uint32_t g_iot_force_reconnect = 0;
volatile uint32_t g_iot_rx_bytes = 0;
volatile uint32_t g_iot_rx_errors = 0;
volatile uint32_t g_iot_rx_ore = 0;
volatile uint32_t g_iot_rx_ne = 0;
volatile uint32_t g_iot_rx_fe = 0;
volatile uint32_t g_iot_rx_pe = 0;
volatile uint32_t g_iot_rx_wr = 0;
volatile uint8_t  g_iot_rx_ring[IOT_RX_RING_SIZE];
volatile uint32_t g_iot_at_probe_req = 0;
volatile uint32_t g_iot_auto_probe = 0;
volatile uint32_t g_iot_at_probe_sent = 0;
volatile uint32_t g_iot_tx_bytes = 0;
volatile uint32_t g_iot_line_count = 0;
volatile uint32_t g_iot_at_ok = 0;
volatile uint32_t g_iot_at_error = 0;
volatile char     g_iot_device_sn[24];
volatile char     g_iot_last_line[IOT_LINE_SIZE];
volatile char     g_iot_last_cmd[IOT_CMD_SIZE];
volatile char     g_iot_last_payload[IOT_PAYLOAD_SIZE];

static void copy_to_volatile(volatile char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;
    if (dst_size == 0U) return;
    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int line_equals(const volatile char *line, const char *text)
{
    while (*text != '\0') {
        if (*line++ != *text++) return 0;
    }
    return (*line == '\0') ? 1 : 0;
}

static int line_starts_with(const volatile char *line, const char *prefix)
{
    while (*prefix != '\0') {
        if (*line++ != *prefix++) return 0;
    }
    return 1;
}

static int line_contains(const volatile char *line, const char *needle)
{
    if (*needle == '\0') return 1;
    for (; *line != '\0'; line++) {
        const volatile char *p = line;
        const char *n = needle;
        while (*p != '\0' && *n != '\0' && *p == *n) {
            p++;
            n++;
        }
        if (*n == '\0') return 1;
    }
    return 0;
}

static uint32_t parse_first_uint_after(const volatile char *line, const char *prefix)
{
    uint32_t value = 0;
    if (!line_starts_with(line, prefix)) return 0xFFFFFFFFU;
    while (*prefix != '\0') {
        line++;
        prefix++;
    }
    while (*line == ' ' || *line == ':') line++;
    if (*line < '0' || *line > '9') return 0xFFFFFFFFU;
    while (*line >= '0' && *line <= '9') {
        value = value * 10U + (uint32_t)(*line - '0');
        line++;
    }
    return value;
}

static void parse_qmt_result(const volatile char *line)
{
    if (line_starts_with(line, "+QMTOPEN:")) {
        s_qmtopen_result = line_contains(line, "0,0") ? 0U : 1U;
        s_qmtopen_seq++;
    } else if (line_starts_with(line, "+QMTCONN:")) {
        s_qmtconn_result = line_contains(line, "0,0,0") ? 0U : 1U;
        s_qmtconn_seq++;
    } else if (line_starts_with(line, "+QMTPUBEX:")) {
        s_qmtpub_result = line_contains(line, "0,0,0") ? 0U : 1U;
        s_qmtpub_seq++;
    } else if (line_starts_with(line, "+QMTSTAT:")) {
        s_qmtstat_seq++;
    }
}

static void commit_line(void)
{
    uint32_t len = s_line_len;
    if (len >= IOT_LINE_SIZE) {
        len = IOT_LINE_SIZE - 1U;
    }
    for (uint32_t i = 0; i < len; i++) {
        g_iot_last_line[i] = s_line_buf[i];
    }
    g_iot_last_line[len] = '\0';
    s_line_len = 0;

    if (len == 0U) return;
    g_iot_line_count++;
    if (line_equals(g_iot_last_line, "OK")) {
        g_iot_at_ok++;
        g_iot_auto_probe = 0;
        s_seen_ok_seq++;
    } else if (line_equals(g_iot_last_line, "ERROR")) {
        g_iot_at_error++;
        s_seen_error_seq++;
    } else if (line_starts_with(g_iot_last_line, "+CSQ:")) {
        uint32_t csq = parse_first_uint_after(g_iot_last_line, "+CSQ");
        if (csq != 0xFFFFFFFFU) {
            g_iot_last_csq = csq;
        }
    }
    parse_qmt_result(g_iot_last_line);
}

static void push_rx_byte(uint8_t b)
{
    g_iot_rx_ring[g_iot_rx_wr & (IOT_RX_RING_SIZE - 1U)] = b;
    g_iot_rx_wr++;
    g_iot_rx_bytes++;

    if (b == '>') {
        g_iot_prompt_count++;
    }
    if (b == '\r' || b == '\n') {
        commit_line();
        return;
    }
    if (s_line_len < (IOT_LINE_SIZE - 1U)) {
        s_line_buf[s_line_len++] = (char)b;
    } else {
        s_line_len = 0;
        g_iot_rx_errors++;
    }
}

static void init_device_sn(void)
{
    if (g_iot_device_sn[0] != '\0') return;
#ifdef UNIT_TEST
    copy_to_volatile(g_iot_device_sn, sizeof(g_iot_device_sn), "UPB-TEST-001");
#else
    const uint32_t *uid = (const uint32_t *)0x1FFF7A10U;
    uint32_t folded = uid[0] ^ uid[1] ^ uid[2];
    char sn[24];
    (void)snprintf(sn, sizeof(sn), "UPB-%08lX", (unsigned long)folded);
    copy_to_volatile(g_iot_device_sn, sizeof(g_iot_device_sn), sn);
#endif
}

static void enter_state(uint32_t state)
{
    g_iot_state = state;
    s_state_started_ms = HAL_GetTick();
}

static void enter_backoff(uint32_t error)
{
    g_iot_last_error = error;
    g_iot_mqtt_connected = 0;
    g_iot_pub_fail_count += (error == IOT_ERR_QMTPUBEX || error == IOT_ERR_PAYLOAD_TOO_LONG) ? 1U : 0U;
    g_iot_reconnect_count++;
    enter_state(IOT_ST_BACKOFF);
    s_backoff_until_ms = HAL_GetTick() + s_backoff_ms;
    if (s_backoff_ms < IOT_BACKOFF_MAX_MS) {
        s_backoff_ms *= 2U;
        if (s_backoff_ms > IOT_BACKOFF_MAX_MS) s_backoff_ms = IOT_BACKOFF_MAX_MS;
    }
}

static HAL_StatusTypeDef send_bytes(const uint8_t *data, uint16_t len)
{
    if (!s_huart || len == 0U) return HAL_ERROR;
    HAL_StatusTypeDef rc = HAL_UART_Transmit(s_huart, (uint8_t *)data, len, 300U);
    if (rc == HAL_OK) {
        g_iot_tx_bytes += len;
    }
    return rc;
}

static int send_text(const char *text)
{
    size_t len = strlen(text);
    if (len > 0xFFFFU) return 0;
    copy_to_volatile(g_iot_last_cmd, sizeof(g_iot_last_cmd), text);
    return send_bytes((const uint8_t *)text, (uint16_t)len) == HAL_OK;
}

static void arm_wait(uint32_t timeout_ms)
{
    uint32_t now = HAL_GetTick();
    s_deadline_ms = now + timeout_ms;
    s_wait_ok_seq = s_seen_ok_seq;
    s_wait_error_seq = s_seen_error_seq;
    s_wait_prompt_seq = g_iot_prompt_count;
    s_wait_qmtopen_seq = s_qmtopen_seq;
    s_wait_qmtconn_seq = s_qmtconn_seq;
    s_wait_qmtpub_seq = s_qmtpub_seq;
    s_wait_qmtstat_seq = s_qmtstat_seq;
}

static int timed_out(void)
{
    return (int32_t)(HAL_GetTick() - s_deadline_ms) >= 0;
}

static int send_cmd_and_wait(const char *cmd, uint32_t wait_state, uint32_t timeout_ms)
{
    if (!send_text(cmd)) {
        enter_backoff(IOT_ERR_UART_TX);
        return 0;
    }
    arm_wait(timeout_ms);
    enter_state(wait_state);
    return 1;
}

static int wait_ok_advance(uint32_t next_state, uint8_t error_also_ok)
{
    if (s_seen_ok_seq != s_wait_ok_seq) {
        enter_state(next_state);
        return 1;
    }
    if (s_seen_error_seq != s_wait_error_seq) {
        if (error_also_ok) {
            enter_state(next_state);
            return 1;
        }
        enter_backoff(IOT_ERR_ERROR_LINE);
        return 0;
    }
    if (timed_out()) {
        enter_backoff(IOT_ERR_TIMEOUT);
        return 0;
    }
    return 0;
}

static void appendf(char *buf, size_t size, size_t *pos, const char *fmt, ...)
{
    if (*pos >= size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(&buf[*pos], size - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= size - *pos) {
        *pos = size;
    } else {
        *pos += (size_t)n;
    }
}

static void append_fixed3(char *buf, size_t size, size_t *pos, int32_t milli)
{
    char sign = '\0';
    if (milli < 0) {
        sign = '-';
        milli = -milli;
    }
    if (sign) {
        appendf(buf, size, pos, "-%ld.%03ld",
                (long)(milli / 1000), (long)(milli % 1000));
    } else {
        appendf(buf, size, pos, "%ld.%03ld",
                (long)(milli / 1000), (long)(milli % 1000));
    }
}

static void append_fixed6(char *buf, size_t size, size_t *pos, int32_t micro)
{
    char sign = '\0';
    if (micro < 0) {
        sign = '-';
        micro = -micro;
    }
    if (sign) {
        appendf(buf, size, pos, "-%ld.%06ld",
                (long)(micro / 1000000), (long)(micro % 1000000));
    } else {
        appendf(buf, size, pos, "%ld.%06ld",
                (long)(micro / 1000000), (long)(micro % 1000000));
    }
}

static const char *json_bool(uint8_t value)
{
    return value ? "true" : "false";
}

static int build_payload(void)
{
    char payload[IOT_PAYLOAD_SIZE];
    size_t pos = 0;
    IotCtrl_TelemetrySnapshot snap = s_snapshot;
    init_device_sn();
    g_iot_seq++;

    appendf(payload, sizeof(payload),
            &pos, "{\"sn\":\"%s\",\"seq\":%lu,\"uptime_ms\":%lu,\"rssi\":",
            (const char *)g_iot_device_sn,
            (unsigned long)g_iot_seq,
            (unsigned long)HAL_GetTick());
    if (g_iot_last_csq == 0xFFFFFFFFU) {
        appendf(payload, sizeof(payload), &pos, "null");
    } else {
        appendf(payload, sizeof(payload), &pos, "%lu", (unsigned long)g_iot_last_csq);
    }
    if (snap.gps_fix && !snap.sampler_stale) {
        appendf(payload, sizeof(payload), &pos, ",\"gps\":{\"fix\":true,\"lat\":");
        append_fixed6(payload, sizeof(payload), &pos, snap.gps_lat_e6);
        appendf(payload, sizeof(payload), &pos, ",\"lon\":");
        append_fixed6(payload, sizeof(payload), &pos, snap.gps_lon_e6);
        appendf(payload, sizeof(payload), &pos, ",\"speed_kt\":");
        append_fixed3(payload, sizeof(payload), &pos, snap.gps_speed_milli_knots);
        appendf(payload, sizeof(payload), &pos, "}");
    } else {
        appendf(payload, sizeof(payload), &pos, ",\"gps\":{\"fix\":false}");
    }
    appendf(payload, sizeof(payload), &pos, ",\"sampler\":{\"stale\":%s,\"ntc_raw\":[",
            json_bool(snap.sampler_stale));
    for (uint32_t i = 0; i < 8U; i++) {
        appendf(payload, sizeof(payload), &pos, "%s%u", (i == 0U) ? "" : ",",
                (unsigned)snap.ntc_raw[i]);
    }
    appendf(payload, sizeof(payload), &pos, "]},\"power\":{\"bat24_v\":");
    append_fixed3(payload, sizeof(payload), &pos, snap.bat24_mv);
    appendf(payload, sizeof(payload), &pos, ",\"bat24_i\":");
    append_fixed3(payload, sizeof(payload), &pos, snap.bat24_ma);
    appendf(payload, sizeof(payload), &pos, ",\"v12_v\":");
    append_fixed3(payload, sizeof(payload), &pos, snap.v12_mv);
    appendf(payload, sizeof(payload),
            &pos,
            "},\"outputs\":{\"boost\":%s,\"load\":%s,\"fan\":%s,\"pump\":%s,\"compressor\":%s}}",
            json_bool(snap.boost_on),
            json_bool(snap.load_on),
            json_bool(snap.fan_on),
            json_bool(snap.pump_on),
            json_bool(snap.compressor_on));

    if (pos >= sizeof(payload)) {
        g_iot_last_payload_len = 0;
        g_iot_last_payload[0] = '\0';
        return 0;
    }
    copy_to_volatile(g_iot_last_payload, sizeof(g_iot_last_payload), payload);
    g_iot_last_payload_len = (uint32_t)strlen(payload);
    return 1;
}

static int send_publish_command(void)
{
    char cmd[IOT_CMD_SIZE];
    if (!build_payload()) {
        enter_backoff(IOT_ERR_PAYLOAD_TOO_LONG);
        return 0;
    }
    init_device_sn();
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QMTPUBEX=0,0,0,0,\"%s/%s/telemetry\",%lu\r\n",
                   IOT_MQTT_TOPIC_PREFIX,
                   (const char *)g_iot_device_sn,
                   (unsigned long)g_iot_last_payload_len);
    if (!send_text(cmd)) {
        enter_backoff(IOT_ERR_UART_TX);
        return 0;
    }
    arm_wait(IOT_CMD_TIMEOUT_MS);
    enter_state(IOT_ST_WAIT_PUB_PROMPT);
    return 1;
}

static void send_at_probe(void)
{
    static const uint8_t at[] = { 'A', 'T', '\r', '\n' };
    if (send_bytes(at, sizeof(at)) == HAL_OK) {
        g_iot_at_probe_sent++;
        s_last_at_ms = HAL_GetTick();
    }
}

void IotCtrl_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    if (!s_huart) return;

    __HAL_UART_CLEAR_FLAG(s_huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE);
    (void)s_huart->Instance->DR;
    g_iot_rx_wr = 0;
    s_line_len = 0;
    g_iot_last_line[0] = '\0';
    g_iot_last_cmd[0] = '\0';
    g_iot_last_payload[0] = '\0';
    s_last_at_ms = HAL_GetTick();
    s_backoff_ms = IOT_BACKOFF_MIN_MS;
    init_device_sn();
    enter_state(IOT_ST_BOOT_WAIT);

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
        g_iot_rx_errors++;
        if (sr & USART_SR_ORE) g_iot_rx_ore++;
        if (sr & USART_SR_NE)  g_iot_rx_ne++;
        if (sr & USART_SR_FE)  g_iot_rx_fe++;
        if (sr & USART_SR_PE)  g_iot_rx_pe++;
        return;
    }
    if (sr & USART_SR_RXNE) {
        uint8_t b = (uint8_t)(reg->DR & 0xFFU);
        push_rx_byte(b);
    }
}

void IotCtrl_SetTelemetrySnapshot(const IotCtrl_TelemetrySnapshot *snapshot)
{
    if (!snapshot) return;
    s_snapshot = *snapshot;
}

void IotCtrl_ForceReconnect(void)
{
    g_iot_force_reconnect = 1U;
}

void IotCtrl_Poll(void)
{
    g_iot_poll_cnt++;
    if (!s_huart) {
        if (g_iot_at_probe_req != 0U) g_iot_at_probe_req = 0U;
        return;
    }

    uint32_t now = HAL_GetTick();
    if (g_iot_at_probe_req != 0U) {
        g_iot_at_probe_req = 0U;
        send_at_probe();
    } else if (g_iot_auto_probe != 0U && g_iot_at_ok == 0U &&
               (uint32_t)(now - s_last_at_ms) >= IOT_AT_RETRY_MS) {
        send_at_probe();
    }

    if (g_iot_enable == 0U) {
        g_iot_mqtt_connected = 0;
        g_iot_state = IOT_ST_DISABLED;
        return;
    }
    if (g_iot_state == IOT_ST_DISABLED) {
        enter_state(IOT_ST_BOOT_WAIT);
    }
    if (g_iot_force_reconnect != 0U) {
        g_iot_force_reconnect = 0U;
        enter_backoff(IOT_ERR_QMTSTAT);
        return;
    }
    if (g_iot_mqtt_connected && s_qmtstat_seq != s_wait_qmtstat_seq) {
        enter_backoff(IOT_ERR_QMTSTAT);
        return;
    }

    switch (g_iot_state) {
    case IOT_ST_BOOT_WAIT:
        if ((uint32_t)(now - s_state_started_ms) >= IOT_BOOT_DELAY_MS) {
            (void)send_cmd_and_wait("ATE0\r\n", IOT_ST_WAIT_ATE0, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_ATE0:
        if (wait_ok_advance(IOT_ST_WAIT_AT, 0U)) {
            (void)send_cmd_and_wait("AT\r\n", IOT_ST_WAIT_AT, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_AT:
        if (wait_ok_advance(IOT_ST_WAIT_CPIN, 0U)) {
            (void)send_cmd_and_wait("AT+CPIN?\r\n", IOT_ST_WAIT_CPIN, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_CPIN:
        if (wait_ok_advance(IOT_ST_WAIT_CSQ, 0U)) {
            (void)send_cmd_and_wait("AT+CSQ\r\n", IOT_ST_WAIT_CSQ, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_CSQ:
        if (wait_ok_advance(IOT_ST_WAIT_CEREG, 0U)) {
            (void)send_cmd_and_wait("AT+CEREG?\r\n", IOT_ST_WAIT_CEREG, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_CEREG:
        if (wait_ok_advance(IOT_ST_WAIT_CGATT, 0U)) {
            (void)send_cmd_and_wait("AT+CGATT?\r\n", IOT_ST_WAIT_CGATT, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_CGATT:
        if (wait_ok_advance(IOT_ST_WAIT_QICSGP, 0U)) {
            (void)send_cmd_and_wait("AT+QICSGP=1,1,\"ctnet\",\"\",\"\",1\r\n",
                                    IOT_ST_WAIT_QICSGP, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_QICSGP:
        if (wait_ok_advance(IOT_ST_WAIT_QIACT, 0U)) {
            (void)send_cmd_and_wait("AT+QIACT=1\r\n", IOT_ST_WAIT_QIACT, 15000U);
        }
        break;
    case IOT_ST_WAIT_QIACT:
        if (wait_ok_advance(IOT_ST_WAIT_QMTCFG_VERSION, 1U)) {
            (void)send_cmd_and_wait("AT+QMTCFG=\"version\",0,4\r\n",
                                    IOT_ST_WAIT_QMTCFG_VERSION, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_QMTCFG_VERSION:
        if (wait_ok_advance(IOT_ST_WAIT_QMTCFG_PDPCID, 0U)) {
            (void)send_cmd_and_wait("AT+QMTCFG=\"pdpcid\",0,1\r\n",
                                    IOT_ST_WAIT_QMTCFG_PDPCID, IOT_CMD_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_QMTCFG_PDPCID:
        if (wait_ok_advance(IOT_ST_WAIT_QMTDISC, 0U)) {
            (void)send_cmd_and_wait("AT+QMTDISC=0\r\n", IOT_ST_WAIT_QMTDISC, IOT_SHORT_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_QMTDISC:
        if (wait_ok_advance(IOT_ST_WAIT_QMTCLOSE, 1U)) {
            (void)send_cmd_and_wait("AT+QMTCLOSE=0\r\n", IOT_ST_WAIT_QMTCLOSE, IOT_SHORT_TIMEOUT_MS);
        }
        break;
    case IOT_ST_WAIT_QMTCLOSE:
        if (wait_ok_advance(IOT_ST_WAIT_QMTOPEN, 1U)) {
            char cmd[IOT_CMD_SIZE];
            (void)snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%u\r\n",
                           IOT_MQTT_HOST, (unsigned)IOT_MQTT_PORT);
            if (send_text(cmd)) {
                arm_wait(IOT_OPEN_TIMEOUT_MS);
                enter_state(IOT_ST_WAIT_QMTOPEN);
            } else {
                enter_backoff(IOT_ERR_UART_TX);
            }
        }
        break;
    case IOT_ST_WAIT_QMTOPEN:
        if (s_qmtopen_seq != s_wait_qmtopen_seq) {
            if (s_qmtopen_result == 0U) {
                char cmd[IOT_CMD_SIZE];
                init_device_sn();
                (void)snprintf(cmd, sizeof(cmd), "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n",
                               (const char *)g_iot_device_sn,
                               IOT_MQTT_USERNAME,
                               IOT_MQTT_PASSWORD);
                if (send_text(cmd)) {
                    arm_wait(IOT_CONN_TIMEOUT_MS);
                    enter_state(IOT_ST_WAIT_QMTCONN);
                } else {
                    enter_backoff(IOT_ERR_UART_TX);
                }
            } else {
                enter_backoff(IOT_ERR_QMTOPEN);
            }
        } else if (timed_out()) {
            enter_backoff(IOT_ERR_TIMEOUT);
        }
        break;
    case IOT_ST_WAIT_QMTCONN:
        if (s_qmtconn_seq != s_wait_qmtconn_seq) {
            if (s_qmtconn_result == 0U) {
                g_iot_mqtt_connected = 1;
                g_iot_last_error = IOT_ERR_NONE;
                s_backoff_ms = IOT_BACKOFF_MIN_MS;
                s_wait_qmtstat_seq = s_qmtstat_seq;
                s_last_pub_ms = now - IOT_PUB_INTERVAL_MS;
                enter_state(IOT_ST_CONNECTED);
            } else {
                enter_backoff(IOT_ERR_QMTCONN);
            }
        } else if (timed_out()) {
            enter_backoff(IOT_ERR_TIMEOUT);
        }
        break;
    case IOT_ST_CONNECTED:
        g_iot_mqtt_connected = 1;
        if ((uint32_t)(now - s_last_pub_ms) >= IOT_PUB_INTERVAL_MS) {
            (void)send_publish_command();
        }
        break;
    case IOT_ST_WAIT_PUB_PROMPT:
        if (g_iot_prompt_count != s_wait_prompt_seq) {
            if (send_bytes((const uint8_t *)g_iot_last_payload,
                           (uint16_t)g_iot_last_payload_len) == HAL_OK) {
                arm_wait(IOT_PUB_TIMEOUT_MS);
                enter_state(IOT_ST_WAIT_PUB_RESULT);
            } else {
                enter_backoff(IOT_ERR_UART_TX);
            }
        } else if (s_seen_error_seq != s_wait_error_seq) {
            enter_backoff(IOT_ERR_QMTPUBEX);
        } else if (timed_out()) {
            enter_backoff(IOT_ERR_TIMEOUT);
        }
        break;
    case IOT_ST_WAIT_PUB_RESULT:
        if (s_qmtpub_seq != s_wait_qmtpub_seq) {
            if (s_qmtpub_result == 0U) {
                g_iot_pub_ok_count++;
                g_iot_last_error = IOT_ERR_NONE;
                s_last_pub_ms = now;
                s_wait_qmtstat_seq = s_qmtstat_seq;
                enter_state(IOT_ST_CONNECTED);
            } else {
                enter_backoff(IOT_ERR_QMTPUBEX);
            }
        } else if (s_seen_error_seq != s_wait_error_seq) {
            enter_backoff(IOT_ERR_QMTPUBEX);
        } else if (timed_out()) {
            enter_backoff(IOT_ERR_TIMEOUT);
        }
        break;
    case IOT_ST_BACKOFF:
        if ((int32_t)(now - s_backoff_until_ms) >= 0) {
            enter_state(IOT_ST_BOOT_WAIT);
        }
        break;
    default:
        enter_backoff(IOT_ERR_TIMEOUT);
        break;
    }
}
