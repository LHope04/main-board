#ifndef __INA226_H
#define __INA226_H

#include "stm32f4xx_hal.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t            dev_addr;   /* 7-bit address left-shifted (HAL convention) */
    uint16_t           cal_val;    /* calibration register value */
    float              cur_lsb;    /* current LSB in Amperes */
    float              voltage_V;
    float              current_A;
    float              power_W;
} INA226_Device;

void              INA226_Init(INA226_Device *dev, I2C_HandleTypeDef *hi2c,
                              uint8_t addr, uint16_t cal, float lsb);
HAL_StatusTypeDef INA226_UpdateData(INA226_Device *dev);
float             INA226_GetVoltage(const INA226_Device *dev);
float             INA226_GetCurrent(const INA226_Device *dev);
float             INA226_GetPower(const INA226_Device *dev);

#endif /* __INA226_H */
