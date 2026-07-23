#ifndef __SAMPLER_COMM_H
#define __SAMPLER_COMM_H

#include "stm32f4xx_hal.h"

/*
 * SamplerComm — USART3 protocol for the 综合采样板 link.
 *
 * Frame format (identical to esp_comm — easy to reuse mental model):
 *     [0xAA] [CMD] [LEN] [PAYLOAD ... LEN bytes] [XOR]
 *     XOR = CMD ^ LEN ^ payload bytes
 *
 * Command set:
 *   0x10  采→主  LEN=16  8 × u16 LE NTC ADC raw values, 100ms cadence
 *   0x12  采→主  LEN=4   selftest_status[1] + version_lo + version_hi + reserved
 *   0x20  主→采  LEN=1   heartbeat sequence number, 500ms cadence
 *
 * Hardware:
 *   USART3 PD8 TX / PD9 RX, 115200 8N1, AF7. Connector U2 (4P GH1.25).
 *
 * The sampler board fw is not yet written (stage 7 only implements the
 * receiver side on the main MCU). When the sampler is offline,
 * SensorAcq_IsStale() returns 1 after 500ms of silence.
 */

#define SAMPLER_FRAME_HEADER  0xAAU
#define SAMPLER_CMD_NTC       0x10U
#define SAMPLER_CMD_SELFTEST  0x12U
#define SAMPLER_CMD_HEARTBEAT 0x20U

typedef struct {
    uint8_t  fix;
    int32_t  lat_e6;
    int32_t  lon_e6;
    int32_t  speed_milli_knots;
    uint32_t last_update_ms;
} SamplerComm_GpsSnapshot;

void SamplerComm_Init(UART_HandleTypeDef *huart);
void SamplerComm_RxISR(void);                  /* USART3_IRQHandler 调用 */
void SamplerComm_Poll(void);                   /* 主循环调; dispatch 已收完整帧 */
void SamplerComm_SendHeartbeat(uint8_t seq);   /* 0x20 帧 */
void SamplerComm_GetGpsSnapshot(SamplerComm_GpsSnapshot *out);

#endif /* __SAMPLER_COMM_H */
