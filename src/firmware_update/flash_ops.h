#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../bootloader/flash_partitions.h"

/**
 * Flash Operations for Firmware Update
 *
 * Provides safe wrappers for RP2040 flash operations with:
 * - Automatic interrupt handling
 * - Alignment verification
 * - Progress callbacks
 * - Watchdog feeding during long operations
 * - CRC32 validation
 */

// Progress callback function type
// Called periodically during long operations (erase, write)
// Parameters: current_bytes, total_bytes, user_data
typedef void (*flash_progress_callback_t)(uint32_t current, uint32_t total, void *user_data);

/**
 * Flash operation result codes
 */
typedef enum {
    FLASH_OP_SUCCESS = 0,
    FLASH_OP_ERROR_INVALID_PARAM,
    FLASH_OP_ERROR_NOT_ALIGNED,
    FLASH_OP_ERROR_OUT_OF_RANGE,
    FLASH_OP_ERROR_VERIFY_FAILED,
    FLASH_OP_ERROR_CRC_MISMATCH,
    FLASH_OP_ERROR_TIMEOUT
} flash_op_result_t;

/**
 * Initialize flash operations module
 */
void flash_ops_init(void);

/**
 * Erase flash sectors in a firmware bank
 *
 * @param bank Firmware bank to erase
 * @param progress_callback Optional progress callback (can be NULL)
 * @param user_data User data passed to callback
 * @return Operation result
 */
flash_op_result_t flash_erase_bank(firmware_bank_t bank,
                                    flash_progress_callback_t progress_callback,
                                    void *user_data);

/**
 * Erase specific flash region
 *
 * @param offset Flash offset (relative to FLASH_BASE_ADDRESS, must be 4KB-aligned)
 * @param size Number of bytes to erase (must be multiple of 4KB)
 * @param progress_callback Optional progress callback (can be NULL)
 * @param user_data User data passed to callback
 * @return Operation result
 */
flash_op_result_t flash_erase_region(uint32_t offset, uint32_t size,
                                      flash_progress_callback_t progress_callback,
                                      void *user_data);

/**
 * Write data to flash
 *
 * @param offset Flash offset (relative to FLASH_BASE_ADDRESS, must be 256-byte aligned)
 * @param data Pointer to source data
 * @param size Number of bytes to write (must be multiple of 256)
 * @param progress_callback Optional progress callback (can be NULL)
 * @param user_data User data passed to callback
 * @return Operation result
 */
flash_op_result_t flash_write(uint32_t offset, const uint8_t *data, uint32_t size,
                               flash_progress_callback_t progress_callback,
                               void *user_data);

/**
 * Write data to flash and verify
 *
 * @param offset Flash offset (relative to FLASH_BASE_ADDRESS, must be 256-byte aligned)
 * @param data Pointer to source data
 * @param size Number of bytes to write (must be multiple of 256)
 * @param progress_callback Optional progress callback (can be NULL)
 * @param user_data User data passed to callback
 * @return Operation result
 */
flash_op_result_t flash_write_and_verify(uint32_t offset, const uint8_t *data, uint32_t size,
                                          flash_progress_callback_t progress_callback,
                                          void *user_data);

/**
 * Read data from flash
 *
 * @param offset Flash offset (relative to FLASH_BASE_ADDRESS)
 * @param data Pointer to destination buffer
 * @param size Number of bytes to read
 * @return Operation result
 */
flash_op_result_t flash_read(uint32_t offset, uint8_t *data, uint32_t size);

/**
 * Verify flash contents match expected data
 *
 * @param offset Flash offset (relative to FLASH_BASE_ADDRESS)
 * @param expected Pointer to expected data
 * @param size Number of bytes to verify
 * @return Operation result
 */
flash_op_result_t flash_verify(uint32_t offset, const uint8_t *expected, uint32_t size);

/**
 * Calculate CRC32 of flash region
 *
 * @param offset Flash offset (relative to FLASH_BASE_ADDRESS)
 * @param size Number of bytes to checksum
 * @param crc32_out Output: CRC32 checksum
 * @param progress_callback Optional progress callback (can be NULL)
 * @param user_data User data passed to callback
 * @return Operation result
 */
flash_op_result_t flash_calculate_crc32(uint32_t offset, uint32_t size, uint32_t *crc32_out,
                                         flash_progress_callback_t progress_callback,
                                         void *user_data);

/**
 * Validate firmware in bank
 *
 * @param bank Firmware bank to validate
 * @param expected_crc32 Expected CRC32 checksum
 * @param expected_size Expected firmware size
 * @param actual_crc32_out Output: Actual CRC32 calculated (can be NULL)
 * @param progress_callback Optional progress callback (can be NULL)
 * @param user_data User data passed to callback
 * @return Operation result
 */
flash_op_result_t flash_validate_firmware(firmware_bank_t bank,
                                           uint32_t expected_crc32,
                                           uint32_t expected_size,
                                           uint32_t *actual_crc32_out,
                                           flash_progress_callback_t progress_callback,
                                           void *user_data);

/**
 * Get error string for result code
 *
 * @param result Result code
 * @return Error description string
 */
const char *flash_op_result_to_string(flash_op_result_t result);

/**
 * Feed watchdog (call periodically during long operations)
 */
void flash_ops_feed_watchdog(void);

#endif // FLASH_OPS_H
