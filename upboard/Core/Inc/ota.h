/**
 * @file    Core/Inc/ota.h
 * @brief   App-side OTA interface: erase/write a target slot and
 *          commit it by updating the boot parameter block.
 */

#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_SLOT_A   0U
#define OTA_SLOT_B   1U

/* Erase the target slot (single 128KB sector). Blocks ~1s. */
bool Ota_Begin(uint8_t slot);

/* Program bytes into the target slot at `offset` from its base.
 * `offset` and `len` must be 4-byte aligned; `data` must be word-accessible. */
bool Ota_WriteChunk(uint8_t slot, uint32_t offset, const void *data, uint32_t len);

/* Finalise an image: compute CRC32 over `size` bytes, fill the boot
 * params for this slot, mark it active+pending, and commit via
 * BootParams_Write(). On return the caller typically triggers a reset. */
bool Ota_End(uint8_t slot, uint32_t size, uint32_t version);

/* One-shot self-test: copy the running A image to Slot B, commit, then
 * trigger a system reset. Never returns on success.
 * `copy_size` must cover the current App image (128KB max = whole slot). */
void Ota_SelfTest(uint32_t copy_size);

/* ---- OTA protocol handlers (called from EspComm_Poll) ---- */

/* Status codes match ESP_CMD ACK payload; see esp_comm.h OTA_STATUS_* */

/* Start an OTA session. Picks target slot = opposite of currently-running
 * slot (read from RTC->BKP0R). Erases the target slot. */
uint8_t OtaProto_HandleStart(uint32_t total_size, uint16_t version);

/* Receive one DATA chunk. Enforces sequential offsets. */
uint8_t OtaProto_HandleData(uint32_t offset, const void *data, uint32_t len);

/* Verify CRC32, write boot params, schedule a reset. */
uint8_t OtaProto_HandleEnd(uint32_t expected_crc32);

/* Cancel current session (back to IDLE). Always returns OK. */
uint8_t OtaProto_HandleAbort(void);

/* Set by HandleEnd on success; caller (main loop) polls and triggers reset. */
uint8_t OtaProto_TakeResetRequest(void);

/* True while an OTA session is in progress (between OTA_START OK and
 * OTA_END/OTA_ABORT). Main loop uses this to suspend STATUS frames so
 * they don't interleave with OTA ACKs on the shared UART. */
uint8_t OtaProto_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif
