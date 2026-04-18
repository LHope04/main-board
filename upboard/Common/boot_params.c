/**
 * @file    Common/boot_params.c
 * @brief   A/B redundant boot parameter storage on F407 Flash sectors 2 and 3.
 */

#include "boot_params.h"
#include "crc32.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* CRC covers the struct up to (but not including) self_crc. */
static uint32_t compute_self_crc(const boot_params_t *p)
{
    return crc32_compute(p, offsetof(boot_params_t, self_crc));
}

static bool read_and_verify(uint32_t addr, boot_params_t *out)
{
    const boot_params_t *src = (const boot_params_t *)addr;
    if (src->magic != BOOT_PARAMS_MAGIC) return false;
    if (src->self_crc != compute_self_crc(src)) return false;
    memcpy(out, src, sizeof(*out));
    return true;
}

bool BootParams_Read(boot_params_t *out)
{
    boot_params_t a, b;
    bool ok_a = read_and_verify(BOOT_PARAMS_A_ADDR, &a);
    bool ok_b = read_and_verify(BOOT_PARAMS_B_ADDR, &b);
    if (ok_a && ok_b) {
        /* Both valid: pick the one with higher sequence. Ties favour A. */
        *out = (b.sequence > a.sequence) ? b : a;
        return true;
    }
    if (ok_a) { *out = a; return true; }
    if (ok_b) { *out = b; return true; }
    return false;
}

void BootParams_Default(boot_params_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic     = BOOT_PARAMS_MAGIC;
    out->boot_slot = BOOT_SLOT_A;
    out->self_crc  = compute_self_crc(out);
}

uint32_t BootParams_SelectSlotAddr(const boot_params_t *p)
{
    return (p->boot_slot == BOOT_SLOT_B) ? BOOT_SLOT_B_ADDR : BOOT_SLOT_A_ADDR;
}

/* Sector index for a param block address. Assumes 16KB sectors 0..3. */
static uint32_t sector_for_params_addr(uint32_t addr)
{
    if (addr == BOOT_PARAMS_A_ADDR) return FLASH_SECTOR_2;
    if (addr == BOOT_PARAMS_B_ADDR) return FLASH_SECTOR_3;
    return 0xFFFFFFFFU;
}

static bool erase_param_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err = 0;
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Banks        = FLASH_BANK_1;
    e.Sector       = sector;
    e.NbSectors    = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    return HAL_FLASHEx_Erase(&e, &err) == HAL_OK;
}

static bool program_params(uint32_t base, const boot_params_t *p)
{
    const uint32_t *w = (const uint32_t *)p;
    const uint32_t n  = sizeof(*p) / 4U;
    for (uint32_t i = 0; i < n; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, base + i * 4U, w[i]) != HAL_OK) {
            return false;
        }
    }
    return true;
}

static bool write_one_copy(uint32_t addr, const boot_params_t *p)
{
    uint32_t sector = sector_for_params_addr(addr);
    if (sector == 0xFFFFFFFFU) return false;
    if (!erase_param_sector(sector)) return false;
    return program_params(addr, p);
}

bool BootParams_Write(const boot_params_t *in)
{
    /* Bump sequence to one greater than the highest existing valid copy, so
     * reader can always pick the newest copy even if write order changes. */
    boot_params_t a, b;
    uint32_t max_seq = 0;
    if (read_and_verify(BOOT_PARAMS_A_ADDR, &a) && a.sequence > max_seq) max_seq = a.sequence;
    if (read_and_verify(BOOT_PARAMS_B_ADDR, &b) && b.sequence > max_seq) max_seq = b.sequence;

    boot_params_t fixed = *in;
    fixed.magic    = BOOT_PARAMS_MAGIC;
    fixed.sequence = max_seq + 1U;
    fixed.self_crc = compute_self_crc(&fixed);

    HAL_FLASH_Unlock();
    bool ok_b = write_one_copy(BOOT_PARAMS_B_ADDR, &fixed);
    bool ok_a = write_one_copy(BOOT_PARAMS_A_ADDR, &fixed);
    HAL_FLASH_Lock();

    return ok_a && ok_b;
}
