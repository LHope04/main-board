#ifndef __MCF8329A_H
#define __MCF8329A_H

#include "stm32f4xx_hal.h"

/*
 * TI MCF8329A — sensorless FOC three-phase BLDC driver.
 *
 * Wire protocol (datasheet 7.6.2):
 *   START | (target_id << 1) | W | CW[23:16] | CW[15:8] | CW[7:0] | data... | STOP
 *   - target_id default 0x01 (HAL addr = 0x02)
 *   - Control Word layout:
 *       CW23      OP_R/W (0=write, 1=read)
 *       CW22      CRC_EN (0=no CRC)
 *       CW21:20   DLEN   (00=16-bit, 01=32-bit, 10=64-bit)
 *       CW19:16   MEM_SEC
 *       CW15:12   MEM_PAGE
 *       CW11:0    MEM_ADDR
 *   - Data bytes: LSB byte first.
 *   - Read: write CW with R bit, then RepeatedStart + (id<<1)|R, then read N bytes (LSB first).
 *   - "100-µs delay between every byte" is a recommendation; at 100kHz I2C
 *     each byte already takes ~90µs so we run without explicit pacing.
 */

/* === Register addresses (22-bit linear; we encode SEC/PAGE inside CW) === */
/* Shadow / EEPROM image (0x000080-0x0000AE) — written when motor not spinning */
#define MCF_REG_ISD_CONFIG         0x000080U
#define MCF_REG_REV_DRIVE_CONFIG   0x000082U
#define MCF_REG_MOTOR_STARTUP1     0x000084U
#define MCF_REG_MOTOR_STARTUP2     0x000086U
#define MCF_REG_CLOSED_LOOP1       0x000088U
#define MCF_REG_CLOSED_LOOP2       0x00008AU
#define MCF_REG_CLOSED_LOOP3       0x00008CU
#define MCF_REG_CLOSED_LOOP4       0x00008EU
#define MCF_REG_FAULT_CONFIG1      0x000090U
#define MCF_REG_FAULT_CONFIG2      0x000092U
#define MCF_REG_PIN_CONFIG1        0x0000A4U
#define MCF_REG_DEVICE_CONFIG1     0x0000A6U
#define MCF_REG_DEVICE_CONFIG2     0x0000A8U
#define MCF_REG_PERI_CONFIG1       0x0000AAU
#define MCF_REG_GD_CONFIG1         0x0000ACU
#define MCF_REG_GD_CONFIG2         0x0000AEU

/* RAM / status (read-only or live-control) */
#define MCF_REG_GATE_FAULT_STATUS  0x0000E0U   /* GATE_DRIVER_FAULT_STATUS */
#define MCF_REG_CTRL_FAULT_STATUS  0x0000E2U   /* CONTROLLER_FAULT_STATUS */
#define MCF_REG_ALGO_STATUS        0x0000E4U   /* SYS_ENABLE_FLAG (bit2) etc. */
#define MCF_REG_ALGO_CTRL1         0x0000EAU   /* CLR_FLT bit29, WATCHDOG_TICKLE bit10 */
#define MCF_REG_ALGO_DEBUG1        0x0000ECU   /* bit31 SPEED_OVER_RIDE,
                                                  bits30:16 DIGITAL_SPEED_CTRL (15-bit duty),
                                                  bit15 CLOSED_LOOP_DIS */
#define MCF_REG_ALGO_DEBUG2        0x0000EEU
#define MCF_REG_ALGORITHM_STATE    0x000196U   /* 0=IDLE, 7=OPEN_LOOP, 8/9=CLOSED_LOOP,
                                                  Eh=FAULT, 18h=MPET_FAULT */

/* === Bit masks for ALGO_CTRL1 === */
#define MCF_CTRL1_EEPROM_WRT       (1U << 31)
#define MCF_CTRL1_EEPROM_READ      (1U << 30)
#define MCF_CTRL1_CLR_FLT          (1U << 29)
#define MCF_CTRL1_WATCHDOG_TICKLE  (1U << 10)

/* === Bit masks for ALGO_DEBUG1 === */
#define MCF_ADBG1_SPEED_OVERRIDE   (1U << 31)
#define MCF_ADBG1_CLOSED_LOOP_DIS  (1U << 15)
#define MCF_ADBG1_DUTY_SHIFT       16
#define MCF_ADBG1_DUTY_MASK        (0x7FFFU << MCF_ADBG1_DUTY_SHIFT)  /* 15-bit, full = 0x7FFF */

/* === Bit masks for ALGO_STATUS === */
#define MCF_ALGO_SYS_ENABLE_FLAG   (1U << 2)

/* Default I2C HAL address (target_id 0x01 << 1) */
#define MCF8329A_HAL_ADDR_DEFAULT  0x02U

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t            addr;       /* HAL 7-bit << 1 */
    uint32_t           last_algo_status;
    uint32_t           last_gate_fault;
    uint32_t           last_ctrl_fault;
} MCF8329A_Device;

/* Low-level 32-bit reg I/O */
HAL_StatusTypeDef MCF8329A_Write32(MCF8329A_Device *dev, uint32_t reg22, uint32_t val);
HAL_StatusTypeDef MCF8329A_Read32 (MCF8329A_Device *dev, uint32_t reg22, uint32_t *val);

/* Lifecycle */
void              MCF8329A_Init(MCF8329A_Device *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_hal);
HAL_StatusTypeDef MCF8329A_PowerOn(MCF8329A_Device *dev);
void              MCF8329A_PowerOff(MCF8329A_Device *dev);

/* GPIO sidebands */
void MCF8329A_DRVOff(uint8_t off);     /* PC12 — 1=DRVOFF asserted (driver disabled) */
void MCF8329A_Wake(uint8_t en);        /* PD0  — 1=awake */
void MCF8329A_SetDir(uint8_t cw);      /* PD3  — 0=CW, 1=reverse */
void MCF8329A_Brake(uint8_t engage);   /* PD4  — 1=brake engaged */
uint8_t MCF8329A_ReadFaultPin(void);   /* PD5  — 1=fault asserted (pin LOW) */

/* High-level helpers (RAM register based, motor not required idle) */
HAL_StatusTypeDef MCF8329A_SpinDuty(MCF8329A_Device *dev, uint16_t duty_15b);
HAL_StatusTypeDef MCF8329A_Stop(MCF8329A_Device *dev);
HAL_StatusTypeDef MCF8329A_RefreshStatus(MCF8329A_Device *dev);
HAL_StatusTypeDef MCF8329A_KickWatchdog(MCF8329A_Device *dev);
HAL_StatusTypeDef MCF8329A_ClearFault(MCF8329A_Device *dev);
HAL_StatusTypeDef MCF8329A_ReadAlgoState(MCF8329A_Device *dev, uint32_t *state);
/* Write minimum shadow config so Align startup produces actual motion.
 * Must be called while motor is in IDLE (chip rejects EEPROM-image writes
 * while spinning). Returns first non-OK rc. */
HAL_StatusTypeDef MCF8329A_LoadMinimumConfig(MCF8329A_Device *dev);

/* Trigger MPET (Motor Parameter Estimation Tool). The chip will:
 *   - Briefly spin the motor at low duty to measure phase resistance,
 *     phase inductance (Ld/Lq) and BEMF constant Ke
 *   - Write measured params to shadow registers (CLOSED_LOOP2 MOTOR_RES/IND
 *     and CLOSED_LOOP3 motor BEMF constant)
 * Caller must ensure motor is stopped (algo_state = IDLE) before calling.
 * Takes ~5-30 seconds to complete; poll ALGO_STATUS_MPET (0xE8) for status. */
HAL_StatusTypeDef MCF8329A_StartMPET(MCF8329A_Device *dev);

#endif /* __MCF8329A_H */
