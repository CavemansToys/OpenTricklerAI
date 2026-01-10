#ifndef FIRMWARE_MANAGER_H
#define FIRMWARE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "../bootloader/flash_partitions.h"
#include "../bootloader/metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Firmware Manager - High-Level OTA Update Orchestration
 *
 * Provides a safe, high-level API for firmware updates with:
 * - Automatic target bank selection
 * - Progress tracking
 * - Validation and verification
 * - Metadata management
 * - Safe activation
 */

// Firmware update state
typedef enum {
    FIRMWARE_UPDATE_IDLE,
    FIRMWARE_UPDATE_PREPARING,
    FIRMWARE_UPDATE_ERASING,
    FIRMWARE_UPDATE_RECEIVING,
    FIRMWARE_UPDATE_VALIDATING,
    FIRMWARE_UPDATE_COMPLETE,
    FIRMWARE_UPDATE_ERROR
} firmware_update_state_t;

// Firmware update status
typedef struct {
    firmware_update_state_t state;
    uint32_t bytes_received;
    uint32_t total_bytes;
    uint32_t progress_percent;
    firmware_bank_t target_bank;
    char error_message[128];
} firmware_update_status_t;

// Firmware info
typedef struct {
    firmware_bank_t bank;
    bool valid;
    uint32_t size;
    uint32_t crc32;
    char version[32];
    uint8_t boot_count;
} firmware_info_t;

/**
 * Initialize firmware manager
 * Must be called once at application startup
 *
 * @return true if initialization successful
 */
bool firmware_manager_init(void);

/**
 * Confirm successful boot (resets boot counter)
 * Call this after application initialization completes successfully
 *
 * @return true if confirmation successful
 */
bool firmware_manager_confirm_boot(void);

/**
 * Check if a rollback occurred on last boot
 *
 * @return true if rollback occurred
 */
bool firmware_manager_did_rollback_occur(void);

/**
 * Clear rollback flag (after user acknowledgment)
 *
 * @return true if clear successful
 */
bool firmware_manager_clear_rollback_flag(void);

/**
 * Get current running firmware bank
 *
 * @return Current bank (BANK_A, BANK_B, or BANK_UNKNOWN)
 */
firmware_bank_t firmware_manager_get_current_bank(void);

/**
 * Get firmware information for a bank
 *
 * @param bank Bank to query
 * @param info Output: Firmware info structure
 * @return true if info retrieved
 */
bool firmware_manager_get_bank_info(firmware_bank_t bank, firmware_info_t *info);

/**
 * Start firmware update
 * Prepares target bank (erases, marks update in progress)
 *
 * @param expected_size Expected firmware size in bytes
 * @param expected_version Expected version string (can be NULL)
 * @return true if update preparation successful
 */
bool firmware_manager_start_update(uint32_t expected_size, const char *expected_version);

/**
 * Write firmware data chunk
 * Call repeatedly with chunks of firmware data
 *
 * @param data Pointer to firmware data
 * @param size Size of data chunk (must be 256-byte aligned except for last chunk)
 * @return true if write successful
 */
bool firmware_manager_write_chunk(const uint8_t *data, uint32_t size);

/**
 * Finalize firmware update
 * Validates firmware, updates metadata
 *
 * @param final_crc32 Expected CRC32 of complete firmware
 * @return true if finalization successful
 */
bool firmware_manager_finalize_update(uint32_t final_crc32);

/**
 * Cancel firmware update
 * Cleans up and returns to idle state
 */
void firmware_manager_cancel_update(void);

/**
 * Activate new firmware and reboot
 * Switches active bank and triggers system reboot
 *
 * WARNING: This function does not return!
 */
void __attribute__((noreturn)) firmware_manager_activate_and_reboot(void);

/**
 * Trigger manual rollback to previous firmware
 *
 * WARNING: This function does not return if successful!
 *
 * @return false if rollback failed (no valid backup)
 */
bool firmware_manager_rollback_and_reboot(void);

/**
 * Get current update status
 *
 * @param status Output: Update status structure
 */
void firmware_manager_get_status(firmware_update_status_t *status);

/**
 * Get progress percentage (0-100)
 *
 * @return Progress percentage
 */
uint32_t firmware_manager_get_progress(void);

/**
 * Check if update is in progress
 *
 * @return true if update in progress
 */
bool firmware_manager_is_update_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_MANAGER_H
