/**
 * @file    Core/Src/ota.c
 * @brief   OTA slot writer + self-test for STM32F407.
 *
 * Slot A = sector 5 @ 0x08020000 (128KB)
 * Slot B = sector 6 @ 0x08040000 (128KB)
 */

#include "ota.h"
#include "esp_comm.h"
#include "boot_params.h"
#include "crc32.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define SLOT_SIZE   (128U * 1024U)

/* Markers the Bootloader writes to RTC->BKP0R before jumping */
#define BOOT_MARK_A   0xA0A0A0A0U
#define BOOT_MARK_B   0xB0B0B0B0U

static uint32_t slot_base_addr(uint8_t slot)
{
    return (slot == OTA_SLOT_B) ? BOOT_SLOT_B_ADDR : BOOT_SLOT_A_ADDR;
}

static uint32_t slot_flash_sector(uint8_t slot)
{
    return (slot == OTA_SLOT_B) ? FLASH_SECTOR_6 : FLASH_SECTOR_5;
}

bool Ota_Begin(uint8_t slot)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err = 0;
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Banks        = FLASH_BANK_1;
    e.Sector       = slot_flash_sector(slot);
    e.NbSectors    = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &err);
    HAL_FLASH_Lock();

    return st == HAL_OK;
}

bool Ota_WriteChunk(uint8_t slot, uint32_t offset, const void *data, uint32_t len)
{
    if ((offset | len) & 0x3U) return false;
    if (offset + len > SLOT_SIZE) return false;

    uint32_t base  = slot_base_addr(slot);
    const uint32_t *w = (const uint32_t *)data;
    uint32_t words = len / 4U;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              base + offset + i * 4U, w[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
    }
    HAL_FLASH_Lock();
    return true;
}

bool Ota_End(uint8_t slot, uint32_t size, uint32_t version)
{
    uint32_t base = slot_base_addr(slot);
    uint32_t crc  = crc32_compute((const void *)base, size);

    boot_params_t p;
    if (!BootParams_Read(&p)) {
        BootParams_Default(&p);
    }
    p.boot_slot           = slot;
    p.slot_valid[slot]    = 1;
    p.slot_size[slot]     = size;
    p.slot_crc32[slot]    = crc;
    p.slot_version[slot]  = version;
    p.pending_update      = 1;
    p.boot_count          = 0;

    return BootParams_Write(&p);
}

/* ---- OTA protocol state machine ---- */

typedef enum {
    OTA_PROTO_IDLE,
    OTA_PROTO_RECEIVING,
} OtaProtoState;

static OtaProtoState s_state       = OTA_PROTO_IDLE;
static uint8_t       s_target_slot = OTA_SLOT_B;
static uint32_t      s_total_size  = 0;
static uint16_t      s_version     = 0;
static uint32_t      s_next_offset = 0;
static uint8_t       s_reset_pending = 0;

static uint8_t pick_target_slot(void)
{
    /* If we're running from B (BKP0R=B), target A. Otherwise target B. */
    return (RTC->BKP0R == BOOT_MARK_B) ? OTA_SLOT_A : OTA_SLOT_B;
}

uint8_t OtaProto_HandleStart(uint32_t total_size, uint16_t version)
{
    if (total_size == 0 || total_size > SLOT_SIZE) return OTA_STATUS_BAD_PARAM;
    if (total_size & 0x3U)                         return OTA_STATUS_BAD_PARAM;

    s_target_slot  = pick_target_slot();
    s_total_size   = total_size;
    s_version      = version;
    s_next_offset  = 0;
    s_reset_pending = 0;

    if (!Ota_Begin(s_target_slot)) {
        s_state = OTA_PROTO_IDLE;
        return OTA_STATUS_FLASH_ERR;
    }

    s_state = OTA_PROTO_RECEIVING;
    return OTA_STATUS_OK;
}

uint8_t OtaProto_HandleData(uint32_t offset, const void *data, uint32_t len)
{
    if (s_state != OTA_PROTO_RECEIVING)            return OTA_STATUS_BAD_STATE;
    if (offset != s_next_offset)                   return OTA_STATUS_BAD_PARAM;
    if (len == 0 || (len & 0x3U))                  return OTA_STATUS_BAD_PARAM;
    if (offset + len > s_total_size)               return OTA_STATUS_BAD_PARAM;

    if (!Ota_WriteChunk(s_target_slot, offset, data, len)) {
        s_state = OTA_PROTO_IDLE;
        return OTA_STATUS_FLASH_ERR;
    }
    s_next_offset += len;
    return OTA_STATUS_OK;
}

uint8_t OtaProto_HandleEnd(uint32_t expected_crc32)
{
    if (s_state != OTA_PROTO_RECEIVING) return OTA_STATUS_BAD_STATE;
    if (s_next_offset != s_total_size)  return OTA_STATUS_BAD_PARAM;

    uint32_t base = (s_target_slot == OTA_SLOT_B) ? BOOT_SLOT_B_ADDR : BOOT_SLOT_A_ADDR;
    uint32_t got  = crc32_compute((const void *)base, s_total_size);
    if (got != expected_crc32) {
        s_state = OTA_PROTO_IDLE;
        return OTA_STATUS_BAD_CRC;
    }

    if (!Ota_End(s_target_slot, s_total_size, s_version)) {
        s_state = OTA_PROTO_IDLE;
        return OTA_STATUS_FLASH_ERR;
    }

    s_state = OTA_PROTO_IDLE;
    s_reset_pending = 1;   /* main loop will reset after ACK is sent */
    return OTA_STATUS_OK;
}

uint8_t OtaProto_HandleAbort(void)
{
    s_state = OTA_PROTO_IDLE;
    s_next_offset = 0;
    return OTA_STATUS_OK;
}

uint8_t OtaProto_TakeResetRequest(void)
{
    uint8_t r = s_reset_pending;
    s_reset_pending = 0;
    return r;
}

uint8_t OtaProto_IsBusy(void)
{
    return (s_state == OTA_PROTO_RECEIVING) ? 1U : 0U;
}

bool Ota_MarkBootOk(void)
{
    boot_params_t p;
    if (!BootParams_Read(&p)) return false;
    if (p.pending_update == 0U && p.boot_count == 0U) return true;

    p.pending_update = 0U;
    p.boot_count     = 0U;
    return BootParams_Write(&p);
}

void Ota_SelfTest(uint32_t copy_size)
{
    if (copy_size > SLOT_SIZE) copy_size = SLOT_SIZE;
    if (copy_size & 0x3U)      copy_size &= ~0x3U;

    if (!Ota_Begin(OTA_SLOT_B)) return;

    /* Copy A → B in 1KB chunks so the Flash API gets periodic breathing room */
    const uint32_t CHUNK = 1024U;
    for (uint32_t off = 0; off < copy_size; off += CHUNK) {
        uint32_t n = copy_size - off;
        if (n > CHUNK) n = CHUNK;
        const void *src = (const void *)(BOOT_SLOT_A_ADDR + off);
        if (!Ota_WriteChunk(OTA_SLOT_B, off, src, n)) return;
    }

    if (!Ota_End(OTA_SLOT_B, copy_size, 1)) return;

    /* Flush + reset. No return. */
    __DSB();
    NVIC_SystemReset();
}
