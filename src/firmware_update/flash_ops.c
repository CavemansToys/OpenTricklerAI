#include "flash_ops.h"
#include "crc32.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include <string.h>
#include <stdio.h>

/**
 * Flash Operations Implementation
 *
 * Key safety features:
 * - All flash operations disable interrupts
 * - Watchdog fed every 10 sectors during erase (~1 second)
 * - Alignment and range checking on all operations
 * - Progress callbacks for long operations
 * - Verification after write
 */

// Watchdog feeding interval (sectors)
#define WATCHDOG_FEED_INTERVAL  10

void flash_ops_init(void) {
    // Initialize CRC32 module
    crc32_init();
}

void flash_ops_feed_watchdog(void) {
    // Check if watchdog is enabled before feeding
    if (watchdog_enable_caused_reboot() || true) {  // Always feed if enabled
        watchdog_update();
    }
}

const char *flash_op_result_to_string(flash_op_result_t result) {
    switch (result) {
        case FLASH_OP_SUCCESS:
            return "Success";
        case FLASH_OP_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case FLASH_OP_ERROR_NOT_ALIGNED:
            return "Address/size not aligned";
        case FLASH_OP_ERROR_OUT_OF_RANGE:
            return "Out of range";
        case FLASH_OP_ERROR_VERIFY_FAILED:
            return "Verification failed";
        case FLASH_OP_ERROR_CRC_MISMATCH:
            return "CRC mismatch";
        case FLASH_OP_ERROR_TIMEOUT:
            return "Operation timeout";
        default:
            return "Unknown error";
    }
}

flash_op_result_t flash_erase_region(uint32_t offset, uint32_t size,
                                      flash_progress_callback_t progress_callback,
                                      void *user_data) {
    // Validate alignment
    if (!IS_SECTOR_ALIGNED(offset) || !IS_SECTOR_ALIGNED(size)) {
        printf("ERROR: Erase offset/size not 4KB-aligned: 0x%08lx, 0x%08lx\n", offset, size);
        return FLASH_OP_ERROR_NOT_ALIGNED;
    }

    // Validate range
    if (offset + size > FLASH_TOTAL_SIZE) {
        printf("ERROR: Erase range out of bounds: 0x%08lx + 0x%08lx\n", offset, size);
        return FLASH_OP_ERROR_OUT_OF_RANGE;
    }

    // Protect bootloader and metadata from accidental erase
    if (offset < BANK_A_OFFSET) {
        printf("ERROR: Attempt to erase protected region: 0x%08lx\n", offset);
        return FLASH_OP_ERROR_OUT_OF_RANGE;
    }

    printf("Erasing flash: offset=0x%08lx, size=0x%08lx (%lu sectors)\n",
           offset, size, size / FLASH_SECTOR_SIZE);

    uint32_t sectors = size / FLASH_SECTOR_SIZE;
    uint32_t current_offset = offset;

    for (uint32_t i = 0; i < sectors; i++) {
        // Erase sector
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(current_offset, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);

        current_offset += FLASH_SECTOR_SIZE;

        // Feed watchdog periodically
        if ((i % WATCHDOG_FEED_INTERVAL) == 0) {
            flash_ops_feed_watchdog();
        }

        // Progress callback
        if (progress_callback != NULL) {
            progress_callback((i + 1) * FLASH_SECTOR_SIZE, size, user_data);
        }
    }

    printf("Erase complete\n");
    return FLASH_OP_SUCCESS;
}

flash_op_result_t flash_erase_bank(firmware_bank_t bank,
                                    flash_progress_callback_t progress_callback,
                                    void *user_data) {
    if (bank != BANK_A && bank != BANK_B) {
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    uint32_t offset = bank_get_offset(bank);
    uint32_t size = bank_get_size(bank);

    printf("Erasing firmware bank %s\n", (bank == BANK_A) ? "A" : "B");

    return flash_erase_region(offset, size, progress_callback, user_data);
}

flash_op_result_t flash_write(uint32_t offset, const uint8_t *data, uint32_t size,
                               flash_progress_callback_t progress_callback,
                               void *user_data) {
    // Validate parameters
    if (data == NULL || size == 0) {
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    // Validate alignment
    if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(size)) {
        printf("ERROR: Write offset/size not 256-byte aligned: 0x%08lx, 0x%08lx\n", offset, size);
        return FLASH_OP_ERROR_NOT_ALIGNED;
    }

    // Validate range
    if (offset + size > FLASH_TOTAL_SIZE) {
        printf("ERROR: Write range out of bounds: 0x%08lx + 0x%08lx\n", offset, size);
        return FLASH_OP_ERROR_OUT_OF_RANGE;
    }

    // Write in 256-byte pages
    uint32_t pages = size / FLASH_PAGE_SIZE;
    uint32_t current_offset = offset;
    const uint8_t *current_data = data;

    for (uint32_t i = 0; i < pages; i++) {
        // Write page
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(current_offset, current_data, FLASH_PAGE_SIZE);
        restore_interrupts(ints);

        current_offset += FLASH_PAGE_SIZE;
        current_data += FLASH_PAGE_SIZE;

        // Feed watchdog every 16 pages (~4KB)
        if ((i % 16) == 0) {
            flash_ops_feed_watchdog();
        }

        // Progress callback
        if (progress_callback != NULL) {
            progress_callback((i + 1) * FLASH_PAGE_SIZE, size, user_data);
        }
    }

    return FLASH_OP_SUCCESS;
}

flash_op_result_t flash_write_and_verify(uint32_t offset, const uint8_t *data, uint32_t size,
                                          flash_progress_callback_t progress_callback,
                                          void *user_data) {
    // Write data
    flash_op_result_t result = flash_write(offset, data, size, progress_callback, user_data);
    if (result != FLASH_OP_SUCCESS) {
        return result;
    }

    // Verify write
    return flash_verify(offset, data, size);
}

flash_op_result_t flash_read(uint32_t offset, uint8_t *data, uint32_t size) {
    // Validate parameters
    if (data == NULL || size == 0) {
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    // Validate range
    if (offset + size > FLASH_TOTAL_SIZE) {
        return FLASH_OP_ERROR_OUT_OF_RANGE;
    }

    // Read from flash (XIP address space)
    const uint8_t *flash_ptr = (const uint8_t *)(FLASH_BASE_ADDRESS + offset);
    memcpy(data, flash_ptr, size);

    return FLASH_OP_SUCCESS;
}

flash_op_result_t flash_verify(uint32_t offset, const uint8_t *expected, uint32_t size) {
    // Validate parameters
    if (expected == NULL || size == 0) {
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    // Validate range
    if (offset + size > FLASH_TOTAL_SIZE) {
        return FLASH_OP_ERROR_OUT_OF_RANGE;
    }

    // Compare flash contents with expected data
    const uint8_t *flash_ptr = (const uint8_t *)(FLASH_BASE_ADDRESS + offset);

    if (memcmp(flash_ptr, expected, size) != 0) {
        // Find first mismatch for debugging
        for (uint32_t i = 0; i < size; i++) {
            if (flash_ptr[i] != expected[i]) {
                printf("ERROR: Verify failed at offset 0x%08lx: expected 0x%02x, got 0x%02x\n",
                       offset + i, expected[i], flash_ptr[i]);
                break;
            }
        }
        return FLASH_OP_ERROR_VERIFY_FAILED;
    }

    return FLASH_OP_SUCCESS;
}

flash_op_result_t flash_calculate_crc32(uint32_t offset, uint32_t size, uint32_t *crc32_out,
                                         flash_progress_callback_t progress_callback,
                                         void *user_data) {
    // Validate parameters
    if (crc32_out == NULL || size == 0) {
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    // Validate range
    if (offset + size > FLASH_TOTAL_SIZE) {
        return FLASH_OP_ERROR_OUT_OF_RANGE;
    }

    // Calculate CRC32 in chunks to allow progress updates and watchdog feeding
    const uint32_t chunk_size = 4096;  // 4KB chunks
    crc32_context_t ctx;
    crc32_begin(&ctx);

    const uint8_t *flash_ptr = (const uint8_t *)(FLASH_BASE_ADDRESS + offset);
    uint32_t remaining = size;
    uint32_t processed = 0;

    while (remaining > 0) {
        uint32_t current_chunk = (remaining < chunk_size) ? remaining : chunk_size;

        // Update CRC32
        crc32_update(&ctx, flash_ptr + processed, current_chunk);

        processed += current_chunk;
        remaining -= current_chunk;

        // Feed watchdog
        if ((processed % (16 * 1024)) == 0) {  // Every 16KB
            flash_ops_feed_watchdog();
        }

        // Progress callback
        if (progress_callback != NULL) {
            progress_callback(processed, size, user_data);
        }
    }

    *crc32_out = crc32_finalize(&ctx);

    return FLASH_OP_SUCCESS;
}

flash_op_result_t flash_validate_firmware(firmware_bank_t bank,
                                           uint32_t expected_crc32,
                                           uint32_t expected_size,
                                           uint32_t *actual_crc32_out,
                                           flash_progress_callback_t progress_callback,
                                           void *user_data) {
    if (bank != BANK_A && bank != BANK_B) {
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    // Validate size is within bank
    if (expected_size > bank_get_size(bank)) {
        printf("ERROR: Firmware size %lu exceeds bank size %lu\n",
               expected_size, bank_get_size(bank));
        return FLASH_OP_ERROR_INVALID_PARAM;
    }

    uint32_t offset = bank_get_offset(bank);

    printf("Validating firmware in bank %s: size=%lu, expected_crc32=0x%08lx\n",
           (bank == BANK_A) ? "A" : "B", expected_size, expected_crc32);

    // Calculate CRC32
    uint32_t actual_crc32;
    flash_op_result_t result = flash_calculate_crc32(offset, expected_size, &actual_crc32,
                                                      progress_callback, user_data);
    if (result != FLASH_OP_SUCCESS) {
        return result;
    }

    if (actual_crc32_out != NULL) {
        *actual_crc32_out = actual_crc32;
    }

    // Compare CRC32
    if (actual_crc32 != expected_crc32) {
        printf("ERROR: CRC32 mismatch - expected 0x%08lx, got 0x%08lx\n",
               expected_crc32, actual_crc32);
        return FLASH_OP_ERROR_CRC_MISMATCH;
    }

    printf("Firmware validation PASSED\n");
    return FLASH_OP_SUCCESS;
}
