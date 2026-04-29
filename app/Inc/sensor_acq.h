#ifndef __SENSOR_ACQ_H
#define __SENSOR_ACQ_H

#include "stm32f4xx_hal.h"

/*
 * V6: NTC sampling moved off-board to the sampler 综合采样板. Main board no
 * longer drives ADC1; instead, raw NTC ADC values arrive over USART3 framed
 * by SamplerComm (阶段 7). This module exposes the same upper-level API as
 * before so business code (status reporting, protection state machine, etc.)
 * doesn't need to care where the data came from.
 *
 * NTC channel mapping (matches old V3 layout, sampler firmware must preserve):
 *   0=2-NTC2  1=2-NTC3  2=2-NTC4  3=2-NTC1 (水温)
 *   4=1-NTC2  5=1-NTC3  6=1-NTC4  7=1-NTC1 (环温)
 */

void     SensorAcq_Init(void);
uint16_t SensorAcq_GetNTC(uint8_t ch);                 /* ch 0~7, raw 12-bit */
float    SensorAcq_NTCToCelsius(uint16_t raw);         /* raw -> °C, NaN if out of range */

/* 阶段 7 新增, USART3 SamplerComm 调: */
void     SensorAcq_OnNtcFrame(const uint8_t *payload16);    /* 8 × u16 LE */
void     SensorAcq_OnSelftestFrame(const uint8_t *p, uint8_t len);
void     SensorAcq_Tick(uint32_t now_ms);              /* stale 检测 + 心跳节拍 */
uint8_t  SensorAcq_IsStale(void);                      /* 1 = 500ms 没收到帧 */

#endif /* __SENSOR_ACQ_H */
