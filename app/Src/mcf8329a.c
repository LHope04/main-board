/**
 * @file    app/Src/mcf8329a.c
 * @brief   TI MCF8329A driver — I2C register access + GPIO sidebands.
 *
 * STAGE 6 SCAFFOLDING — register addresses and bit layouts are placeholders
 * until the datasheet is verified against silicon. The I2C wrappers and
 * GPIO sidebands are usable now; high-level commands assume the placeholder
 * register map and will need a one-pass review when the chip wakes up.
 *
 * IMPORTANT: All register I/O uses HAL_I2C_Mem_* which internally calls
 * HAL_Delay — **do not call any of these from an ISR**. The nFAULT line
 * (PD5) gets read via the GPIO helper from the main loop, never inside the
 * EXTI ISR (if one is later wired up).
 */
#include "mcf8329a.h"

#define I2C_TIMEOUT_MS  100U

/* === Low-level I2C === */

HAL_StatusTypeDef MCF8329A_WriteReg(MCF8329A_Device *dev, uint8_t reg, uint16_t val)
{
    if (!dev || !dev->hi2c) return HAL_ERROR;
    /* TI convention is usually big-endian on the wire. Verify with datasheet
     * once silicon is up; flip the byte order here if reads come back swapped. */
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return HAL_I2C_Mem_Write(dev->hi2c, dev->addr, reg, I2C_MEMADD_SIZE_8BIT,
                             buf, 2, I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef MCF8329A_ReadReg(MCF8329A_Device *dev, uint8_t reg, uint16_t *val)
{
    if (!dev || !dev->hi2c || !val) return HAL_ERROR;
    uint8_t buf[2] = {0};
    HAL_StatusTypeDef rc = HAL_I2C_Mem_Read(dev->hi2c, dev->addr, reg, I2C_MEMADD_SIZE_8BIT,
                                            buf, 2, I2C_TIMEOUT_MS);
    if (rc == HAL_OK) {
        *val = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return rc;
}

/* === GPIO sidebands === */

void MCF8329A_DROff(uint8_t en)
{
    /* PC12 — 1=DROFF active high (driver enabled). Naming is per netlist;
     * the "off" in DROFF is part of the signal name, not its polarity. */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void MCF8329A_Wake(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void MCF8329A_SetDir(uint8_t cw)
{
    /* cw=0 (default) → PD3=LOW; cw=1 (reverse) → PD3=HIGH. */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, cw ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void MCF8329A_Brake(uint8_t release)
{
    /* release=1 → PD4=HIGH = brake released, motor free.
     * release=0 → PD4=LOW  = brake engaged. */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, release ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t MCF8329A_ReadFaultPin(void)
{
    /* PD5 nFAULT: LOW = fault asserted. */
    return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_5) == GPIO_PIN_RESET;
}

/* === Lifecycle === */

void MCF8329A_Init(MCF8329A_Device *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_hal)
{
    if (!dev) return;
    dev->hi2c       = hi2c;
    dev->addr       = addr_hal;
    dev->last_state = 0;
    dev->last_fault = 0;

    /* Force safe sideband state regardless of caller order. */
    MCF8329A_DROff(0);
    MCF8329A_Wake(0);
    MCF8329A_SetDir(0);
    MCF8329A_Brake(0);    /* brake engaged */
}

HAL_StatusTypeDef MCF8329A_PowerOn(MCF8329A_Device *dev)
{
    if (!dev) return HAL_ERROR;

    /* Sideband startup: wake → DROFF on (driver enabled) → release brake. */
    MCF8329A_Wake(1);
    HAL_Delay(2);
    MCF8329A_DROff(1);
    HAL_Delay(2);
    MCF8329A_Brake(1);

    /* TODO(stage 6): write register defaults from datasheet (e.g., speed
     * loop PI gains, current limit, fault mask). Placeholder zero-write. */
    HAL_StatusTypeDef rc = HAL_OK;
    /* rc |= MCF8329A_WriteReg(dev, MCF8329A_REG_CTRL, ENABLE_BIT); */
    return rc;
}

void MCF8329A_PowerOff(MCF8329A_Device *dev)
{
    (void)dev;
    MCF8329A_Brake(0);     /* engage brake first to slow rotor */
    HAL_Delay(50);
    MCF8329A_DROff(0);     /* then disable driver */
    MCF8329A_Wake(0);      /* finally sleep */
}

/* === High-level register commands === */

HAL_StatusTypeDef MCF8329A_SetSpeed(MCF8329A_Device *dev, uint16_t speed_units)
{
    /* TODO(stage 6): unit conversion (RPM → register units). Placeholder
     * passes the value straight through. */
    return MCF8329A_WriteReg(dev, MCF8329A_REG_SPEED, speed_units);
}

HAL_StatusTypeDef MCF8329A_GetState(MCF8329A_Device *dev, uint16_t *state)
{
    HAL_StatusTypeDef rc = MCF8329A_ReadReg(dev, MCF8329A_REG_STATE, state);
    if (rc == HAL_OK && state) dev->last_state = *state;
    return rc;
}

HAL_StatusTypeDef MCF8329A_GetFault(MCF8329A_Device *dev, uint16_t *fault)
{
    HAL_StatusTypeDef rc = MCF8329A_ReadReg(dev, MCF8329A_REG_FAULT, fault);
    if (rc == HAL_OK && fault) dev->last_fault = *fault;
    return rc;
}

HAL_StatusTypeDef MCF8329A_ClearFault(MCF8329A_Device *dev)
{
    /* TODO(stage 6): write the fault-clear bit per datasheet. Placeholder
     * issues a benign register write that may or may not actually clear. */
    return MCF8329A_WriteReg(dev, MCF8329A_REG_CTRL, 0x0000U);
}
