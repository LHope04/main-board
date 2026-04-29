/**
 * @file    app/Src/sensor_acq.c
 * @brief   Sensor acquisition for V6 board.
 *
 * STAGE 1 (current): minimal scaffolding — keeps the public API but raw NTC
 * data is zero until SamplerComm starts feeding frames in 阶段 7.
 * NTCToCelsius() retains the V3 lookup-table conversion (electrical circuit
 * unchanged on the sampler board).
 *
 * STAGE 7: SamplerComm calls SensorAcq_OnNtcFrame() with 8×u16 raw ADC values.
 * Tick(now_ms) tracks staleness (>500ms no frame → stale).
 */
#include "sensor_acq.h"
#include <math.h>
#include <string.h>

typedef struct {
    uint16_t raw[8];
    uint32_t last_update_tick;
    uint8_t  stale;
    uint8_t  selftest_ok;
    uint16_t fw_version;
} sensor_state_t;

static sensor_state_t s_st;

void SensorAcq_Init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    s_st.stale = 1;     /* 上电默认 stale, 等收到帧后清 */
}

uint16_t SensorAcq_GetNTC(uint8_t ch)
{
    if (ch >= 8) return 0;
    return s_st.raw[ch];
}

void SensorAcq_OnNtcFrame(const uint8_t *p)
{
    /* TODO(stage 7): SamplerComm dispatch 调本函数, payload = 8 × u16 LE */
    if (!p) return;
    for (int i = 0; i < 8; i++) {
        s_st.raw[i] = (uint16_t)p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8);
    }
    s_st.last_update_tick = HAL_GetTick();
    s_st.stale = 0;
}

void SensorAcq_OnSelftestFrame(const uint8_t *p, uint8_t len)
{
    if (!p || len < 4) return;
    s_st.selftest_ok = p[0];
    s_st.fw_version  = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
}

void SensorAcq_Tick(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - s_st.last_update_tick) > 500U) {
        s_st.stale = 1;
    }
}

uint8_t SensorAcq_IsStale(void) { return s_st.stale; }

/*
 * NTC: ZY103, R25=10kΩ, B=3950, 1%
 * 电路: NTC-VCC(3.3V) -- NTC -- IN+ -- R7(8.25kΩ) -- PGND
 *       运放增益: Vout = (16*Vp - 33) / 3, Vp = 3.3 * 8.25 / (R_ntc + 8.25)
 * 实测饱和: ADC_LOW=700 (<46°C), ADC_HIGH=2736 (>57°C)
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
    if (raw <= NTC_ADC_LOW)  return NAN;
    if (raw >= NTC_ADC_HIGH) return NAN;

    int lo = 0, hi = NTC_T_MAX - NTC_T_MIN - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (s_ntc_table[mid] <= raw) lo = mid;
        else hi = mid - 1;
    }

    float t0   = NTC_T_MIN + lo;
    float adc0 = s_ntc_table[lo];
    float adc1 = s_ntc_table[lo + 1];
    return t0 + (raw - adc0) / (adc1 - adc0);
}
