/**
 * @file    app/Src/mcf8329a.c
 * @brief   TI MCF8329A driver — 24-bit Control Word + 32-bit reg access.
 *
 * I2C target ID 0x01 (HAL addr 0x02). Protocol per datasheet §7.6.2:
 *
 *   Write 32-bit:
 *     [(0x01<<1)|W]  CW0  CW1  CW2  D0(LSB) D1 D2 D3(MSB)
 *
 *   Read 32-bit:
 *     [(0x01<<1)|W]  CW0  CW1  CW2     <-- CW with R bit set
 *     <RepeatedStart>
 *     [(0x01<<1)|R]  D0(LSB) D1 D2 D3(MSB)
 *
 * Control Word (3 bytes, big-endian on the wire):
 *   CW23     = OP_R/W  (0=W, 1=R)
 *   CW22     = CRC_EN  (we always 0)
 *   CW21:20  = DLEN    (01=32-bit)
 *   CW19:16  = MEM_SEC
 *   CW15:12  = MEM_PAGE
 *   CW11:0   = MEM_ADDR
 *
 * No CRC. HAL_I2C_Mem_* can't represent a 3-byte register address, so we
 * use HAL_I2C_Master_Transmit / Master_Receive directly. The driver is
 * main-loop only (HAL_Delay inside) — never call from ISR.
 */
#include "mcf8329a.h"
#include <string.h>

#define I2C_TIMEOUT_MS  50U

/* === Diagnostic globals (SWD watch) === */
volatile uint32_t g_mcf_w_ok       = 0;
volatile uint32_t g_mcf_w_err      = 0;
volatile uint32_t g_mcf_r_ok       = 0;
volatile uint32_t g_mcf_r_err      = 0;
volatile uint32_t g_mcf_last_err   = 0xFFU;   /* HAL_StatusTypeDef of last failure */
volatile uint32_t g_mcf_last_algo  = 0;
volatile uint32_t g_mcf_last_gate  = 0;
volatile uint32_t g_mcf_last_ctrl  = 0;

/* === Control Word builders === */

static inline void mcf_pack_cw(uint8_t cw[3], uint8_t op_rw, uint8_t dlen,
                               uint32_t reg22)
{
    uint8_t  sec  = (uint8_t)((reg22 >> 16) & 0x0FU);
    uint8_t  page = (uint8_t)((reg22 >> 12) & 0x0FU);
    uint16_t addr = (uint16_t)(reg22 & 0x0FFFU);

    cw[0] = (uint8_t)(((op_rw & 0x01U) << 7) |  /* CW23 */
                      (0U << 6) |               /* CW22 CRC_EN = 0 */
                      ((dlen & 0x03U) << 4) |   /* CW21:20 */
                      (sec & 0x0FU));           /* CW19:16 */
    cw[1] = (uint8_t)(((page & 0x0FU) << 4) |   /* CW15:12 */
                      ((addr >> 8) & 0x0FU));   /* CW11:8 */
    cw[2] = (uint8_t)(addr & 0xFFU);            /* CW7:0 */
}

/* === 32-bit reg I/O === */

HAL_StatusTypeDef MCF8329A_Write32(MCF8329A_Device *dev, uint32_t reg22, uint32_t val)
{
    if (!dev || !dev->hi2c) return HAL_ERROR;

    uint8_t pkt[7];
    mcf_pack_cw(pkt, /*W*/0, /*32-bit*/0x01U, reg22);
    pkt[3] = (uint8_t)(val        & 0xFFU);   /* LSB first */
    pkt[4] = (uint8_t)((val >>  8) & 0xFFU);
    pkt[5] = (uint8_t)((val >> 16) & 0xFFU);
    pkt[6] = (uint8_t)((val >> 24) & 0xFFU);

    HAL_StatusTypeDef rc = HAL_I2C_Master_Transmit(dev->hi2c, dev->addr,
                                                   pkt, sizeof(pkt), I2C_TIMEOUT_MS);
    if (rc == HAL_OK) {
        g_mcf_w_ok++;
    } else {
        g_mcf_w_err++;
        g_mcf_last_err = (uint32_t)rc;
    }
    return rc;
}

HAL_StatusTypeDef MCF8329A_Read32(MCF8329A_Device *dev, uint32_t reg22, uint32_t *val)
{
    if (!dev || !dev->hi2c || !val) return HAL_ERROR;

    /* Datasheet shows RepeatedStart between CW write and data read. STM32 HAL
     * blocking API can't do that without I2C IRQ wiring, so we issue a STOP
     * between phase 1 (CW) and phase 2 (data). Most TI BLDC drivers accept it;
     * if reads come back garbage, wire I2C3 IRQ + use HAL_I2C_Master_Seq_*_IT. */
    uint8_t cw[3];
    mcf_pack_cw(cw, /*R*/1, /*32-bit*/0x01U, reg22);

    HAL_StatusTypeDef rc = HAL_I2C_Master_Transmit(dev->hi2c, dev->addr,
                                                    cw, sizeof(cw), I2C_TIMEOUT_MS);
    if (rc != HAL_OK) {
        g_mcf_r_err++;
        g_mcf_last_err = (uint32_t)rc;
        return rc;
    }

    uint8_t buf[4] = {0};
    rc = HAL_I2C_Master_Receive(dev->hi2c, dev->addr, buf, sizeof(buf), I2C_TIMEOUT_MS);
    if (rc == HAL_OK) {
        *val = (uint32_t)buf[0]
             | ((uint32_t)buf[1] <<  8)
             | ((uint32_t)buf[2] << 16)
             | ((uint32_t)buf[3] << 24);
        g_mcf_r_ok++;
    } else {
        g_mcf_r_err++;
        g_mcf_last_err = (uint32_t)rc;
    }
    return rc;
}

/* === GPIO sidebands === */

void MCF8329A_DRVOff(uint8_t off)
{
    /* PC12 — DRVOFF: HIGH disables driver, LOW enables (datasheet name).
     * Default state in gpio.c is LOW = driver enabled. */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, off ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void MCF8329A_Wake(uint8_t en)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void MCF8329A_SetDir(uint8_t cw)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, cw ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void MCF8329A_Brake(uint8_t engage)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, engage ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t MCF8329A_ReadFaultPin(void)
{
    return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_5) == GPIO_PIN_RESET;
}

/* === Lifecycle === */

/* Diagnostic: actual address found by scan (HAL 8-bit << 1). 0 = scan failed. */
volatile uint8_t g_mcf_scan_addr = 0;

void MCF8329A_Init(MCF8329A_Device *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_hal)
{
    if (!dev) return;
    memset(dev, 0, sizeof(*dev));
    dev->hi2c = hi2c;

    /* Wake + driver-enable first so the chip will ACK on the bus. */
    MCF8329A_DRVOff(0);
    MCF8329A_Wake(1);
    MCF8329A_SetDir(0);
    MCF8329A_Brake(0);
    HAL_Delay(5);

    /* Auto-scan: Motor Studio / EEPROM writes can change the chip's I2C
     * target address (0x01 / 0x5A factory default / arbitrary). Probe
     * caller's hint first, fall back to known defaults, then full scan. */
    const uint8_t hints[] = {
        addr_hal ? addr_hal : MCF8329A_HAL_ADDR_DEFAULT,
        MCF8329A_HAL_ADDR_DEFAULT,    /* 0x02  (target 0x01) */
        0xB4U,                         /* 0xB4  (target 0x5A factory default) */
    };
    for (uint8_t i = 0; i < sizeof(hints); i++) {
        if (HAL_I2C_IsDeviceReady(hi2c, hints[i], 2, 10) == HAL_OK) {
            dev->addr = hints[i];
            g_mcf_scan_addr = hints[i];
            return;
        }
    }
    for (uint8_t a = 1; a < 128; a++) {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint8_t)(a << 1), 2, 5) == HAL_OK) {
            dev->addr = (uint8_t)(a << 1);
            g_mcf_scan_addr = dev->addr;
            return;
        }
    }
    /* Nothing answered — keep caller's hint, every Write32/Read32 will fail. */
    dev->addr = addr_hal ? addr_hal : MCF8329A_HAL_ADDR_DEFAULT;
    g_mcf_scan_addr = 0;
}

HAL_StatusTypeDef MCF8329A_PowerOn(MCF8329A_Device *dev)
{
    if (!dev) return HAL_ERROR;
    MCF8329A_Wake(1);
    HAL_Delay(5);
    MCF8329A_DRVOff(0);
    HAL_Delay(2);
    MCF8329A_Brake(0);
    return HAL_OK;
}

void MCF8329A_PowerOff(MCF8329A_Device *dev)
{
    (void)dev;
    MCF8329A_Brake(1);
    HAL_Delay(50);
    MCF8329A_DRVOff(1);
    MCF8329A_Wake(0);
}

/* === High-level: spin via ALGO_DEBUG1 override === */

/* Runtime flag (SWD-tunable, no rebuild):
 *   0 = normal FOC
 *   1 = pure open-loop  (CLOSED_LOOP_DIS + FORCE_SLOW_FIRST_CYCLE_EN)
 *   2 = force align     (FORCE_ALIGN — DC current to fixed phase, max torque)
 * For 模式 2 配合 MOTOR_STARTUP1 ALIGN_OR_SLOW_CURRENT=Fh + FAULT_CONFIG1
 * LOCK_ILIMIT_MODE/MTR_LCK_MODE 关闭, 是芯片能给的最大启动推力. */
volatile uint8_t g_mcf_force_open_loop = 0;

HAL_StatusTypeDef MCF8329A_SpinDuty(MCF8329A_Device *dev, uint16_t duty_15b)
{
    if (duty_15b > 0x7FFFU) duty_15b = 0x7FFFU;
    uint32_t v = MCF_ADBG1_SPEED_OVERRIDE
               | (((uint32_t)duty_15b) << MCF_ADBG1_DUTY_SHIFT);
    if (g_mcf_force_open_loop == 1U) {
        v |= MCF_ADBG1_CLOSED_LOOP_DIS | MCF_ADBG1_FORCE_SLOW_FIRST;
    } else if (g_mcf_force_open_loop == 2U) {
        v |= MCF_ADBG1_FORCE_ALIGN;       /* DC 灌入, 最大转矩 */
    }
    return MCF8329A_Write32(dev, MCF_REG_ALGO_DEBUG1, v);
}

HAL_StatusTypeDef MCF8329A_Stop(MCF8329A_Device *dev)
{
    /* Clear override → algorithm goes idle. */
    return MCF8329A_Write32(dev, MCF_REG_ALGO_DEBUG1, 0U);
}

HAL_StatusTypeDef MCF8329A_RefreshStatus(MCF8329A_Device *dev)
{
    if (!dev) return HAL_ERROR;
    HAL_StatusTypeDef rc;
    rc = MCF8329A_Read32(dev, MCF_REG_ALGO_STATUS, &dev->last_algo_status);
    if (rc == HAL_OK) g_mcf_last_algo = dev->last_algo_status;
    if (rc != HAL_OK) return rc;

    rc = MCF8329A_Read32(dev, MCF_REG_GATE_FAULT_STATUS, &dev->last_gate_fault);
    if (rc == HAL_OK) g_mcf_last_gate = dev->last_gate_fault;
    if (rc != HAL_OK) return rc;

    rc = MCF8329A_Read32(dev, MCF_REG_CTRL_FAULT_STATUS, &dev->last_ctrl_fault);
    if (rc == HAL_OK) g_mcf_last_ctrl = dev->last_ctrl_fault;
    return rc;
}

HAL_StatusTypeDef MCF8329A_KickWatchdog(MCF8329A_Device *dev)
{
    /* In I2C mode, host must set bit10 within every EXT_WD_CONFIG period
     * or the chip latches a watchdog fault and stops accepting commands. */
    return MCF8329A_Write32(dev, MCF_REG_ALGO_CTRL1, MCF_CTRL1_WATCHDOG_TICKLE);
}

HAL_StatusTypeDef MCF8329A_ClearFault(MCF8329A_Device *dev)
{
    /* Set CLR_FLT and tickle the WD in the same write — write-only bits
     * auto-clear, so doing both at once is fine. */
    return MCF8329A_Write32(dev, MCF_REG_ALGO_CTRL1,
                            MCF_CTRL1_CLR_FLT | MCF_CTRL1_WATCHDOG_TICKLE);
}

HAL_StatusTypeDef MCF8329A_ReadAlgoState(MCF8329A_Device *dev, uint32_t *state)
{
    return MCF8329A_Read32(dev, MCF_REG_ALGORITHM_STATE, state);
}

HAL_StatusTypeDef MCF8329A_StartMPET(MCF8329A_Device *dev)
{
    /* ALGO_DEBUG2 (0xEE) bit fields per datasheet 7.8.2.2:
     *   bit 5 MPET_CMD            — initiate MPET routine
     *   bit 2 MPET_KE             — measure BEMF constant
     *   bit 1 MPET_MECH           — measure mechanical (R, L)
     *   bit 0 MPET_WRITE_SHADOW   — write measured params to shadow on completion
     * All four set = full MPET with auto-update. */
    return MCF8329A_Write32(dev, MCF_REG_ALGO_DEBUG2,
                            (1U << 5) | (1U << 2) | (1U << 1) | (1U << 0));
}

HAL_StatusTypeDef MCF8329A_LoadCompressorProfile(MCF8329A_Device *dev,
                                                uint32_t *mismatch_mask,
                                                uint32_t *verified_count)
{
    /* Full EEPROM shadow image from Motor Studio "BLDC_Pump_3A_200Hz_MCF8329A_v1".
     *
     * Target compressor: 生利达 ZW50-3.3-24 (全封闭旋转式直流变频压缩机)
     *   - 8 poles (4 pole pairs)
     *   - 3000~4500 RPM operating range (200~300 Hz electrical)
     *   - 24V bus, 3.3A nominal input, 80W rated
     *   - Phase R: 0.244Ω L-L → ~0.122Ω/phase (Y winding)
     *   - Phase L: 0.076mH L-L → ~38µH/phase (Ld=Lq, non-salient)
     *   - Ke: 1.98 Vrms/Krpm L-L
     *   - Demag peak: 8 Apk (HARD CEILING — current limits stay below this)
     *   - Recommended ramp: 60 rpm/s; hold 1+ min at 40rps/80rps during startup
     *
     * The Pump_3A_200Hz preset is electrically close (3A nom, similar Hz range)
     * and verified to spin the compressor. Sub-optimal at the high end (200Hz vs
     * 300Hz cap, different ramp). For production: capture a fresh Motor Studio
     * JSON with this compressor connected and replace the config table below. */
    static const struct { uint32_t addr; uint32_t val; } cfg[] = {
        { 0x000080U, 0x6462AC20U },   /* ISD_CONFIG */
        { 0x000082U, 0xA8200000U },   /* REV_DRIVE_CONFIG */
        { 0x000084U, 0x4D6948D4U },   /* MOTOR_STARTUP1 */
        { 0x000086U, 0xA92A6007U },   /* MOTOR_STARTUP2 */
        /* CLOSED_LOOP1: pump config 0x285B91B8 had FG_DIV=1 (2-pole assumption);
         * compressor is 8-pole → FG_DIV=4 → byte changes 0x91→0x94, parity OK */
        { 0x000088U, 0x285B94B8U },   /* CLOSED_LOOP1 (FG_DIV adjusted for 8-pole) */
        { 0x00008AU, 0x8BAD5988U },   /* CLOSED_LOOP2 */
        { 0x00008CU, 0x26000004U },   /* CLOSED_LOOP3 */
        { 0x00008EU, 0x08D904B0U },   /* CLOSED_LOOP4 */
        { 0x000090U, 0xE7F240A7U },   /* FAULT_CONFIG1 */
        { 0x000092U, 0x79004000U },   /* FAULT_CONFIG2 */
        { 0x000094U, 0x00000000U },   /* REF_PROFILES1 */
        { 0x000096U, 0x00000000U },   /* REF_PROFILES2 */
        { 0x000098U, 0x00000000U },   /* REF_PROFILES3 */
        { 0x00009AU, 0x80000000U },   /* REF_PROFILES4 */
        { 0x00009CU, 0x00000000U },   /* REF_PROFILES5 */
        { 0x00009EU, 0x00000000U },   /* REF_PROFILES6 */
        { 0x0000A0U, 0xA433407DU },   /* INT_ALGO_1 */
        { 0x0000A2U, 0x000003E7U },   /* INT_ALGO_2 */
        { 0x0000A4U, 0x0200000AU },   /* PIN_CONFIG */
        { 0x0000A8U, 0x0000000EU },   /* DEVICE_CONFIG2 */
        { 0x0000AAU, 0x8BB57988U },   /* PERI_CONFIG1 */
        { 0x0000ACU, 0x9C450103U },   /* GD_CONFIG1 */
        { 0x0000AEU, 0x000000CCU },   /* GD_CONFIG2 */
        /* Contains I2C target address; write last and re-probe below. */
        { 0x0000A6U, 0x00101462U },   /* DEVICE_CONFIG1 */
    };

    if (!dev || !dev->hi2c) return HAL_ERROR;
    if (mismatch_mask) *mismatch_mask = 0U;
    if (verified_count) *verified_count = 0U;

    const uint8_t old_addr = dev->addr;
    for (uint32_t i = 0; i < sizeof(cfg)/sizeof(cfg[0]); i++) {
        HAL_StatusTypeDef rc = MCF8329A_Write32(dev, cfg[i].addr, cfg[i].val);
        if (rc != HAL_OK) return rc;
        /* small gap so chip can latch each shadow write */
        HAL_Delay(2);
    }

    HAL_Delay(5);
    if (HAL_I2C_IsDeviceReady(dev->hi2c, MCF8329A_HAL_ADDR_DEFAULT, 2, 10) == HAL_OK) {
        dev->addr = MCF8329A_HAL_ADDR_DEFAULT;
        g_mcf_scan_addr = dev->addr;
    } else if (HAL_I2C_IsDeviceReady(dev->hi2c, old_addr, 2, 10) == HAL_OK) {
        dev->addr = old_addr;
        g_mcf_scan_addr = dev->addr;
    } else {
        g_mcf_scan_addr = 0U;
        return HAL_ERROR;
    }

    uint32_t mismatches = 0U;
    uint32_t matches = 0U;
    for (uint32_t i = 0; i < sizeof(cfg)/sizeof(cfg[0]); i++) {
        uint32_t readback = 0U;
        HAL_StatusTypeDef rc = MCF8329A_Read32(dev, cfg[i].addr, &readback);
        if (rc != HAL_OK) return rc;
        if (readback == cfg[i].val) {
            matches++;
        } else {
            mismatches |= (1UL << i);
        }
    }

    if (mismatch_mask) *mismatch_mask = mismatches;
    if (verified_count) *verified_count = matches;
    return (mismatches == 0U) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef MCF8329A_LoadMinimumConfig(MCF8329A_Device *dev)
{
    return MCF8329A_LoadCompressorProfile(dev, NULL, NULL);
}

HAL_StatusTypeDef MCF8329A_LoadRecommendedDefaults(MCF8329A_Device *dev,
                                                   uint32_t *mismatch_mask,
                                                   uint32_t *verified_count)
{
    /* TI MCF8329A datasheet SLLSFQ7 (November 2023), Table 8-1:
     * "Recommended Default Values". These are the documented default EEPROM
     * settings chosen by TI for reliable startup and closed-loop operation.
     *
     * DEVICE_CONFIG1 is intentionally written last because it contains the
     * I2C target address. The official value selects target 0x01; depending on
     * silicon state, that address may become active immediately. */
    static const struct { uint32_t addr; uint32_t val; } cfg[] = {
        { 0x000080U, 0x64A2D4A1U },   /* ISD_CONFIG */
        { 0x000082U, 0x48300000U },   /* REV_DRIVE_CONFIG */
        { 0x000084U, 0x10A64CC0U },   /* MOTOR_STARTUP1 */
        { 0x000086U, 0x2D81C007U },   /* MOTOR_STARTUP2 */
        { 0x000088U, 0x1D7181B8U },   /* CLOSED_LOOP1 */
        { 0x00008AU, 0x0AAD0000U },   /* CLOSED_LOOP2 */
        { 0x00008CU, 0x00000000U },   /* CLOSED_LOOP3 */
        { 0x00008EU, 0x000004B0U },   /* CLOSED_LOOP4 */
        { 0x000090U, 0x465A31A6U },   /* FAULT_CONFIG1 */
        { 0x000092U, 0x71422888U },   /* FAULT_CONFIG2 */
        { 0x000094U, 0x00000000U },   /* REF_PROFILES1 */
        { 0x000096U, 0x00000000U },   /* REF_PROFILES2 */
        { 0x000098U, 0x00000004U },   /* REF_PROFILES3 */
        { 0x00009AU, 0x00000000U },   /* REF_PROFILES4 */
        { 0x00009CU, 0x00000000U },   /* REF_PROFILES5 */
        { 0x00009EU, 0x00000000U },   /* REF_PROFILES6 */
        { 0x0000A0U, 0x0946027DU },   /* INT_ALGO_1 */
        { 0x0000A2U, 0x020082E3U },   /* INT_ALGO_2 */
        { 0x0000A4U, 0x40032309U },   /* PIN_CONFIG */
        { 0x0000A8U, 0x03E8C00CU },   /* DEVICE_CONFIG2 */
        { 0x0000AAU, 0x69845CC0U },   /* PERI_CONFIG1 */
        { 0x0000ACU, 0x0000807BU },   /* GD_CONFIG1 */
        { 0x0000AEU, 0x00000400U },   /* GD_CONFIG2 */
        { 0x0000A6U, 0x00100002U },   /* DEVICE_CONFIG1 — address field, last */
    };

    if (!dev || !dev->hi2c) return HAL_ERROR;
    if (mismatch_mask) *mismatch_mask = 0U;
    if (verified_count) *verified_count = 0U;

    const uint8_t old_addr = dev->addr;
    for (uint32_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        HAL_StatusTypeDef rc = MCF8329A_Write32(dev, cfg[i].addr, cfg[i].val);
        if (rc != HAL_OK) return rc;
        HAL_Delay(2);
    }

    /* DEVICE_CONFIG1 may move the live target to the official address 0x01.
     * Prefer it if present; otherwise retain the address that accepted the
     * configuration write. */
    HAL_Delay(5);
    if (HAL_I2C_IsDeviceReady(dev->hi2c, MCF8329A_HAL_ADDR_DEFAULT, 2, 10) == HAL_OK) {
        dev->addr = MCF8329A_HAL_ADDR_DEFAULT;
        g_mcf_scan_addr = dev->addr;
    } else if (HAL_I2C_IsDeviceReady(dev->hi2c, old_addr, 2, 10) == HAL_OK) {
        dev->addr = old_addr;
        g_mcf_scan_addr = dev->addr;
    } else {
        g_mcf_scan_addr = 0U;
        return HAL_ERROR;
    }

    uint32_t mismatches = 0U;
    uint32_t matches = 0U;
    for (uint32_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        uint32_t readback = 0U;
        HAL_StatusTypeDef rc = MCF8329A_Read32(dev, cfg[i].addr, &readback);
        if (rc != HAL_OK) return rc;
        if (readback == cfg[i].val) {
            matches++;
        } else {
            mismatches |= (1UL << i);
        }
    }

    if (mismatch_mask) *mismatch_mask = mismatches;
    if (verified_count) *verified_count = matches;
    return (mismatches == 0U) ? HAL_OK : HAL_ERROR;
}
