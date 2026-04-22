/**
 * @file    Common/boot_params.h
 * @brief   OTA boot parameter block shared by Bootloader and App.
 *
 * Two redundant copies live at PARAMS_A and PARAMS_B (16KB each, sector 2/3).
 * Each copy is protected by a CRC32 over everything except self_crc itself,
 * so either copy being intact is sufficient to recover.
 */

#ifndef COMMON_BOOT_PARAMS_H
#define COMMON_BOOT_PARAMS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_PARAMS_MAGIC     0xA5A5A5A5U

#define BOOT_PARAMS_A_ADDR    0x08008000U  /* sector 2 */
#define BOOT_PARAMS_B_ADDR    0x0800C000U  /* sector 3 */

#define BOOT_SLOT_A           0U
#define BOOT_SLOT_B           1U

#define BOOT_SLOT_A_ADDR      0x08020000U  /* sector 5 */
#define BOOT_SLOT_B_ADDR      0x08040000U  /* sector 6 */

typedef struct {
    uint32_t magic;              /* BOOT_PARAMS_MAGIC */
    uint32_t boot_slot;          /* 0=A, 1=B */
    uint32_t slot_valid[2];      /* non-zero = slot holds a verified image */
    uint32_t slot_size[2];       /* image size in bytes */
    uint32_t slot_crc32[2];      /* CRC32 of slot_size bytes starting at slot base */
    uint32_t slot_version[2];
    uint32_t pending_update;     /* 1 = freshly written image awaiting first boot */
    uint32_t boot_count;         /* incremented by Bootloader, cleared by App once stable */
    uint32_t sequence;           /* monotonic write counter; reader picks max valid seq */
    uint32_t reserved[3];        /* pad for future fields without bumping struct */
    uint32_t self_crc;           /* CRC32 over all preceding fields */
} boot_params_t;

/* Read params: tries A first, falls back to B. Returns true on success. */
bool BootParams_Read(boot_params_t *out);

/* Write both copies (B first, then A) so an interrupted write leaves A intact. */
bool BootParams_Write(const boot_params_t *in);

/* Populate a struct with safe defaults (slot A active, everything else zero). */
void BootParams_Default(boot_params_t *out);

/* Pick which slot address to jump to based on the params block. */
uint32_t BootParams_SelectSlotAddr(const boot_params_t *p);

#ifdef __cplusplus
}
#endif

#endif
