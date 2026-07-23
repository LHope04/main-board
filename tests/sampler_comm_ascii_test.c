#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sampler_comm.h"

static USART_TypeDef fake_usart3;
static UART_HandleTypeDef fake_huart3 = { &fake_usart3 };
static uint32_t fake_tick;
static uint16_t last_ntc[8];
static uint32_t ntc_frames;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data,
                                    uint16_t len, uint32_t timeout)
{
    (void)huart;
    (void)data;
    (void)len;
    (void)timeout;
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

void SensorAcq_OnNtcFrame(const uint8_t *payload16)
{
    ntc_frames++;
    for (uint8_t i = 0; i < 8U; i++) {
        last_ntc[i] = (uint16_t)payload16[i * 2U] |
                      ((uint16_t)payload16[i * 2U + 1U] << 8);
    }
}

void SensorAcq_OnSelftestFrame(const uint8_t *p, uint8_t len)
{
    (void)p;
    (void)len;
}

static void feed_byte(uint8_t b)
{
    fake_usart3.SR = USART_SR_RXNE;
    fake_usart3.DR = b;
    SamplerComm_RxISR();
}

static void feed_text(const char *text)
{
    while (*text) {
        feed_byte((uint8_t)*text++);
    }
}

int main(void)
{
    SamplerComm_Init(&fake_huart3);

    feed_text("ACC:0.00,0.01,0.98 GYR:0.0,0.1,0.0 T:28.5 "
              "GPS:22.300001,114.100002,0.3kt "
              "NTC:1800,1801,1802,1803,1804,1805,1806,1807\r\n");
    SamplerComm_Poll();

    SamplerComm_GpsSnapshot gps = {0};
    SamplerComm_GetGpsSnapshot(&gps);

    assert(ntc_frames == 1U);
    assert(last_ntc[0] == 1800U);
    assert(last_ntc[7] == 1807U);
    assert(gps.fix == 1U);
    assert(gps.lat_e6 == 22300001);
    assert(gps.lon_e6 == 114100002);
    assert(gps.speed_milli_knots == 300);

    feed_text("ACC:0.00,0.00,0.00 GYR:0.0,0.0,0.0 T:0.0 "
              "GPS:no_fix NTC:1,2,3,4,5,6,7,8\r\n");
    SamplerComm_Poll();
    SamplerComm_GetGpsSnapshot(&gps);
    assert(gps.fix == 0U);

    puts("sampler_comm ASCII GPS test passed");
    return 0;
}
