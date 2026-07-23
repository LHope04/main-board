#ifndef __IOT_CTRL_H
#define __IOT_CTRL_H

#include "stm32f4xx_hal.h"

/*
 * EC801E MQTT uplink over USART2 PA2/PA3, 115200 8N1.
 * The module owns the AT session: PDP activation, MQTT open/connect, QMTPUBEX
 * telemetry publish, reconnect/backoff, and J-Link-readable diagnostics.
 */

typedef struct {
    uint16_t ntc_raw[8];
    int32_t  gps_lat_e6;
    int32_t  gps_lon_e6;
    int32_t  gps_speed_milli_knots;
    int32_t  bat24_mv;
    int32_t  bat24_ma;
    int32_t  v12_mv;
    uint8_t  gps_fix;
    uint8_t  boost_on;
    uint8_t  load_on;
    uint8_t  fan_on;
    uint8_t  pump_on;
    uint8_t  compressor_on;
    uint8_t  sampler_stale;
} IotCtrl_TelemetrySnapshot;

void IotCtrl_Init(UART_HandleTypeDef *huart);
void IotCtrl_RxISR(void);
void IotCtrl_Poll(void);
void IotCtrl_SetTelemetrySnapshot(const IotCtrl_TelemetrySnapshot *snapshot);
void IotCtrl_ForceReconnect(void);

extern volatile uint32_t g_iot_poll_cnt;
extern volatile uint32_t g_iot_enable;
extern volatile uint32_t g_iot_state;
extern volatile uint32_t g_iot_last_error;
extern volatile uint32_t g_iot_mqtt_connected;
extern volatile uint32_t g_iot_reconnect_count;
extern volatile uint32_t g_iot_pub_ok_count;
extern volatile uint32_t g_iot_pub_fail_count;
extern volatile uint32_t g_iot_seq;
extern volatile uint32_t g_iot_last_payload_len;
extern volatile uint32_t g_iot_last_csq;
extern volatile uint32_t g_iot_prompt_count;
extern volatile uint32_t g_iot_force_reconnect;
extern volatile uint32_t g_iot_rx_bytes;
extern volatile uint32_t g_iot_rx_errors;
extern volatile uint32_t g_iot_rx_ore;
extern volatile uint32_t g_iot_rx_ne;
extern volatile uint32_t g_iot_rx_fe;
extern volatile uint32_t g_iot_rx_pe;
extern volatile uint32_t g_iot_rx_wr;
extern volatile uint8_t  g_iot_rx_ring[128];
extern volatile uint32_t g_iot_at_probe_req;
extern volatile uint32_t g_iot_auto_probe;
extern volatile uint32_t g_iot_at_probe_sent;
extern volatile uint32_t g_iot_tx_bytes;
extern volatile uint32_t g_iot_line_count;
extern volatile uint32_t g_iot_at_ok;
extern volatile uint32_t g_iot_at_error;
extern volatile char     g_iot_device_sn[24];
extern volatile char     g_iot_last_line[96];
extern volatile char     g_iot_last_cmd[160];
extern volatile char     g_iot_last_payload[384];

#endif /* __IOT_CTRL_H */
