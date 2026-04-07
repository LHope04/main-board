#include "sensor_acq.h"
#include <math.h>

static ADC_HandleTypeDef *s_hadc;
static uint16_t s_adc_buf[8] __attribute__((aligned(4)));

void SensorAcq_Init(ADC_HandleTypeDef *hadc)
{
    s_hadc = hadc;
}

void SensorAcq_Start(void)
{
    HAL_ADC_Start_DMA(s_hadc, (uint32_t *)s_adc_buf, 8);
}

uint16_t SensorAcq_GetNTC(uint8_t ch)
{
    if (ch >= 8) return 0;
    return s_adc_buf[ch];
}

const uint16_t *SensorAcq_GetAllNTC(void)
{
    return s_adc_buf;
}

/*
 * NTC: ZY103, R25=10kΩ, B=3950, 1%
 * 电路: NTC-VCC(3.3V) -- NTC -- IN+ -- R7(8.25kΩ) -- PGND
 *       运放增益: Vout = (16*Vp - 33) / 3, Vp = 3.3 * 8.25 / (R_ntc + 8.25)
 * 实测饱和: ADC_LOW=700 (<46°C), ADC_HIGH=2736 (>57°C)
 * 线性区ADC查表(46~57°C，每1°C一个条目，从低温到高温ADC递增)
 */
#define NTC_T_MIN     46
#define NTC_T_MAX     57
#define NTC_ADC_LOW   700
#define NTC_ADC_HIGH  2736

static const uint16_t s_ntc_table[] = {
    828, 1017, 1204, 1386, 1565, 1741, 1912, 2080, 2245, 2406, 2563, 2716
    /* 46°C  47°C  48°C  49°C  50°C  51°C  52°C  53°C  54°C  55°C  56°C  57°C */
};

float SensorAcq_NTCToCelsius(uint16_t raw)
{
    if (raw <= NTC_ADC_LOW)  return NAN;  /* 断路或 <46°C */
    if (raw >= NTC_ADC_HIGH) return NAN;  /* 短路或 >57°C */

    /* 二分查找所在区间 */
    int lo = 0, hi = NTC_T_MAX - NTC_T_MIN - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (s_ntc_table[mid] <= raw) lo = mid;
        else hi = mid - 1;
    }

    /* 线性插值 */
    float t0 = NTC_T_MIN + lo;
    float adc0 = s_ntc_table[lo];
    float adc1 = s_ntc_table[lo + 1];
    return t0 + (raw - adc0) / (adc1 - adc0);
}
