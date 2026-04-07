#include "ina226.h"

static void INA226_WriteReg(INA226_Device *dev, uint8_t reg, uint16_t data)
{
    uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    HAL_I2C_Mem_Write(dev->hi2c, dev->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
}

static uint16_t INA226_ReadReg(INA226_Device *dev, uint8_t reg)
{
    uint8_t buf[2] = {0};
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->dev_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100) == HAL_OK) {
        return (uint16_t)((buf[0] << 8) | buf[1]);
    }
    return 0;
}

void INA226_Init(INA226_Device *dev, I2C_HandleTypeDef *hi2c,
                 uint8_t addr, uint16_t cal, float lsb)
{
    dev->hi2c      = hi2c;
    dev->dev_addr  = addr;
    dev->cal_val   = cal;
    dev->cur_lsb   = lsb;
    dev->voltage_V = 0.0f;
    dev->current_A = 0.0f;
    dev->power_W   = 0.0f;

    /* Config: 16-avg, 1.1ms conversion, continuous shunt+bus */
    INA226_WriteReg(dev, 0x00, 0x4527);
    /* Calibration */
    INA226_WriteReg(dev, 0x05, dev->cal_val);
}

HAL_StatusTypeDef INA226_UpdateData(INA226_Device *dev)
{
    dev->voltage_V = (float)INA226_ReadReg(dev, 0x02) * 0.00125f;
    dev->current_A = (float)(int16_t)INA226_ReadReg(dev, 0x04) * dev->cur_lsb;
    dev->power_W   = (float)INA226_ReadReg(dev, 0x03) * (dev->cur_lsb * 25.0f);
    return HAL_OK;
}

float INA226_GetVoltage(const INA226_Device *dev) { return dev->voltage_V; }
float INA226_GetCurrent(const INA226_Device *dev) { return dev->current_A; }
float INA226_GetPower(const INA226_Device *dev)   { return dev->power_W;   }
