/**
 * @file    Common/crc32.h
 * @brief   CRC32 (IEEE 802.3, poly 0xEDB88320) shared by Bootloader and App.
 */

#ifndef COMMON_CRC32_H
#define COMMON_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32_compute(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
