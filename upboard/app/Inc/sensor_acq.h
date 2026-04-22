#ifndef __SENSOR_ACQ_H
#define __SENSOR_ACQ_H

#include "stm32f4xx_hal.h"

/*
 * NTC channel mapping (ADC1 scan order, adc_buf index):
 *   0=2-NTC2 (PA0/IN0)   1=2-NTC3 (PA1/IN1)   2=2-NTC4 (PA2/IN2)   3=2-NTC1 (PA3/IN3)
 *   4=1-NTC2 (PA4/IN4)   5=1-NTC3 (PA5/IN5)   6=1-NTC4 (PA6/IN6)   7=1-NTC1 (PA7/IN7)
 */

void            SensorAcq_Init(ADC_HandleTypeDef *hadc);
void            SensorAcq_Start(void);
uint16_t        SensorAcq_GetNTC(uint8_t ch);      /* ch 0~7, returns raw 12-bit value */
const uint16_t *SensorAcq_GetAllNTC(void);         /* pointer to internal adc_buf[8]   */
float           SensorAcq_NTCToCelsius(uint16_t raw); /* raw -> °C, NaN if out of range */

#endif /* __SENSOR_ACQ_H */
