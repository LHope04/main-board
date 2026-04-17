#ifndef __ESP_COMM_H
#define __ESP_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* Frame protocol constants */
#define ESP_FRAME_HEADER      0xAA
#define ESP_MAX_PAYLOAD       32
#define ESP_MAX_FRAME         (1 + 1 + 1 + ESP_MAX_PAYLOAD + 1)  /* 36 */

/* Command codes */
#define ESP_CMD_PING          0x01   /* C3 -> STM32 */
#define ESP_CMD_SET_GEAR      0x20   /* C3 -> STM32 */
#define ESP_CMD_OTA_SELFTEST  0xF0   /* C3 -> STM32, payload=0, triggers A→B self-copy+reset */
#define ESP_CMD_PING_ACK      0x81   /* STM32 -> C3 */
#define ESP_CMD_STATUS        0xA0   /* STM32 -> C3 */

/* Gear command received from ESP32 */
typedef struct {
    int16_t  gear;       /* 1~10 */
    uint8_t  on;         /* 1=on, 0=off */
    uint8_t  updated;    /* set by ISR/Poll, cleared by application */
} EspComm_GearCmd;

/* Status payload sent to ESP32 */
typedef struct {
    int16_t  water_temp_x10;     /* water temp * 10 */
    uint8_t  battery_pct;        /* 0~100 */
    uint16_t total_power_w;      /* watts */
    uint8_t  error_flags;        /* bit0=over-temp, bit1=dry-burn */
    int16_t  ambient_temp_x10;   /* ambient temp * 10 */
} EspComm_Status;

void            EspComm_Init(UART_HandleTypeDef *huart);
void            EspComm_Poll(void);
void            EspComm_RxISR(void);
void            EspComm_SendStatus(const EspComm_Status *st);
EspComm_GearCmd *EspComm_GetGearCmd(void);
uint8_t          EspComm_TakeOtaSelfTestRequest(void);  /* returns 1 once if requested, then clears */

#ifdef __cplusplus
}
#endif

#endif /* __ESP_COMM_H */
