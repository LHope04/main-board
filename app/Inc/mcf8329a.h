#ifndef __MCF8329A_H
#define __MCF8329A_H

#include "stm32f4xx_hal.h"

/*
 * TI MCF8329A — sensorless trapezoidal/FOC three-phase BLDC driver.
 * V6 main board uses this for the compressor (YSJ).
 *
 * Control surface:
 *   - I2C3 (PA8 SCL / PC9 SDA, 100kHz) for register configuration + speed cmd
 *   - GPIO sidebands (PC12 DROFF, PD0 SPEED_WAKE, PD3 DIR, PD4 BREAK)
 *   - Inputs (PD5 nFAULT, PB9 FG via TIM4_CH4 IC, PC1 SOX via ADC1_IN11)
 *
 * I2C 7-bit address: determined by ADDR_SEL pin tie. **TODO(stage 6): confirm
 * from board schematic + datasheet.** Common defaults: 0x01 (HAL=0x02) or
 * 0x21 (HAL=0x42). The constructor takes the HAL-shifted address so callers
 * stay explicit.
 *
 * Register table — placeholders. **TODO(stage 6): replace with datasheet
 * offsets**. The current values let the compile go through and give the
 * driver layer a stable shape before silicon is in front of us.
 */

/* === Placeholder register map (replace with datasheet) === */
#define MCF8329A_REG_STATE       0x00U   /* RO: algorithm state */
#define MCF8329A_REG_ALGO_CTRL   0x01U   /* RW: AUTO_RUN / BRAKE / DIR bits */
#define MCF8329A_REG_SPEED       0x02U   /* RW: target speed (16-bit, units TBD) */
#define MCF8329A_REG_FAULT       0x03U   /* RO: latched fault status */
#define MCF8329A_REG_CTRL        0x04U   /* RW: EN / SLEEP / FAULT_CLR bits */

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t            addr;       /* HAL 7-bit << 1 (e.g. 0x02 for ADDR=0x01) */
    uint16_t           last_state;
    uint16_t           last_fault;
} MCF8329A_Device;

/* Low-level I2C wrappers */
HAL_StatusTypeDef MCF8329A_WriteReg(MCF8329A_Device *dev, uint8_t reg, uint16_t val);
HAL_StatusTypeDef MCF8329A_ReadReg (MCF8329A_Device *dev, uint8_t reg, uint16_t *val);

/* Lifecycle */
void              MCF8329A_Init(MCF8329A_Device *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_hal);
HAL_StatusTypeDef MCF8329A_PowerOn(MCF8329A_Device *dev);   /* DROFF=H + Wake=H + register defaults */
void              MCF8329A_PowerOff(MCF8329A_Device *dev);  /* Brake=engage + DROFF=L */

/* GPIO sidebands (no I2C needed) */
void MCF8329A_DROff(uint8_t en);          /* PC12 — 1=enabled, 0=disabled */
void MCF8329A_Wake(uint8_t en);           /* PD0  — 1=awake,   0=sleep */
void MCF8329A_SetDir(uint8_t cw);         /* PD3  — 0=CW (default), 1=reverse */
void MCF8329A_Brake(uint8_t release);     /* PD4  — 1=release, 0=engage */
uint8_t MCF8329A_ReadFaultPin(void);      /* PD5  — 1=fault asserted (LOW) */

/* High-level commands (use I2C) */
HAL_StatusTypeDef MCF8329A_SetSpeed(MCF8329A_Device *dev, uint16_t speed_units);
HAL_StatusTypeDef MCF8329A_GetState(MCF8329A_Device *dev, uint16_t *state);
HAL_StatusTypeDef MCF8329A_GetFault(MCF8329A_Device *dev, uint16_t *fault);
HAL_StatusTypeDef MCF8329A_ClearFault(MCF8329A_Device *dev);

#endif /* __MCF8329A_H */
