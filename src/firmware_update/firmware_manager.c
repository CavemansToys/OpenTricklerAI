#include "firmware_manager.h"
#include "flash_ops.h"
#include "crc32.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>

/**
 * Firmware Manager Implementation
 *
 * High-level API for safe OTA firmware updates
 */

// Update context
static struct {
    bool initialized;
    firmware_update_state_t state;
    firmware_bank_t target_bank;
    uint32_t expected_size;
    uint32_t bytes_written;
    uint32_t current_offset;
    char expected_version[32];
    char error_message[128];
    crc32_context_t crc_ctx;
} update_context;

// Helper: Set error state
static void set_error(const char *message) {
    update_context.state = FIRMWARE_UPDATE_ERROR;
    strncpy(update_context.error_message, message, sizeof(update_context.error_message) - 1);
    update_context.error_message[sizeof(update_context.error_message) - 1] = '\0';
    printf("FIRMWARE UPDATE ERROR: %s\n", message);
}

// Helper: Clear error
static void clear_error(void) {
    update_context.error_message[0] = '\0';
}

bool firmware_manager_init(void) {
    if (update_context.initialized) {
        return true;  // Already initialized
    }

    printf("Initializing firmware manager...\n");

    // Initialize flash operations
    flash_ops_init();

    // Initialize metadata
    if (!metadata_init()) {
        printf("ERROR: Failed to initialize metadata\n");
        return false;
    }

    // Initialize update context
    memset(&update_context, 0, sizeof(update_context));
    update_context.state = FIRMWARE_UPDATE_IDLE;
    update_context.initialized = true;

    // Check if update was in progress
    firmware_metadata_t meta;
    if (metadata_read(&meta)) {
        if (meta.update_in_progress == UPDATE_IN_PROGRESS) {
            printf("WARNING: Previous update was interrupted, clearing flag\n");
            metadata_clear_update_in_progress();
        }
    }

    printf("Firmware manager initialized\n");
    return true;
}

bool firmware_manager_confirm_boot(void) {
    printf("Confirming successful boot...\n");
    return metadata_reset_boot_count();
}

bool firmware_manager_did_rollback_occur(void) {
    return metadata_did_rollback_occur();
}

bool firmware_manager_clear_rollback_flag(void) {
    return metadata_clear_rollback_flag();
}

firmware_bank_t firmware_manager_get_current_bank(void) {
    // Determine current bank from program counter
    uint32_t pc = (uint32_t)__builtin_return_address(0);

    if (pc >= BANK_A_ADDRESS && pc < BANK_A_END) {
        return BANK_A;
    } else if (pc >= BANK_B_ADDRESS && pc < BANK_B_END) {
        return BANK_B;
    }

    return BANK_UNKNOWN;
}

bool firmware_manager_get_bank_info(firmware_bank_t bank, firmware_info_t *info) {
    if (info == NULL || (bank != BANK_A && bank != BANK_B)) {
        return false;
    }

    info->bank = bank;

    uint8_t valid;
    if (!metadata_get_bank_info(bank, &info->crc32, &info->size, info->version,
                                 &valid, &info->boot_count)) {
        return false;
    }

    info->valid = (valid == BANK_VALID);

    return true;
}

bool firmware_manager_start_update(uint32_t expected_size, const char *expected_version) {
    if (!update_context.initialized) {
        return false;
    }

    if (update_context.state != FIRMWARE_UPDATE_IDLE) {
        set_error("Update already in progress");
        return false;
    }

    // Validate size
    if (expected_size == 0 || expected_size > BANK_A_SIZE) {
        set_error("Invalid firmware size");
        return false;
    }

    printf("Starting firmware update: size=%lu bytes\n", expected_size);

    update_context.state = FIRMWARE_UPDATE_PREPARING;
    clear_error();

    // Determine target bank (opposite of current)
    firmware_bank_t current_bank = firmware_manager_get_current_bank();
    update_context.target_bank = bank_get_opposite(current_bank);

    if (update_context.target_bank == BANK_UNKNOWN) {
        set_error("Cannot determine target bank");
        return false;
    }

    printf("Target bank: %s\n", (update_context.target_bank == BANK_A) ? "A" : "B");

    // Mark update in progress
    if (!metadata_set_update_in_progress(update_context.target_bank)) {
        set_error("Failed to mark update in progress");
        return false;
    }

    // Save expected size and version
    update_context.expected_size = expected_size;
    update_context.bytes_written = 0;
    update_context.current_offset = bank_get_offset(update_context.target_bank);

    if (expected_version != NULL) {
        strncpy(update_context.expected_version, expected_version,
                sizeof(update_context.expected_version) - 1);
        update_context.expected_version[sizeof(update_context.expected_version) - 1] = '\0';
    } else {
        update_context.expected_version[0] = '\0';
    }

    // Initialize CRC32 context
    crc32_begin(&update_context.crc_ctx);

    // Erase target bank
    update_context.state = FIRMWARE_UPDATE_ERASING;
    printf("Erasing target bank...\n");

    flash_op_result_t result = flash_erase_bank(update_context.target_bank, NULL, NULL);
    if (result != FLASH_OP_SUCCESS) {
        set_error("Failed to erase target bank");
        metadata_clear_update_in_progress();
        update_context.state = FIRMWARE_UPDATE_IDLE;
        return false;
    }

    update_context.state = FIRMWARE_UPDATE_RECEIVING;
    printf("Ready to receive firmware data\n");

    return true;
}

bool firmware_manager_write_chunk(const uint8_t *data, uint32_t size) {
    if (update_context.state != FIRMWARE_UPDATE_RECEIVING) {
        set_error("Not in receiving state");
        return false;
    }

    if (data == NULL || size == 0) {
        set_error("Invalid data");
        return false;
    }

    // Check if this would exceed expected size
    if (update_context.bytes_written + size > update_context.expected_size) {
        set_error("Data exceeds expected firmware size");
        return false;
    }

    // Align size to 256 bytes for flash write (except for last chunk)
    uint32_t write_size = size;
    uint8_t write_buffer[FLASH_PAGE_SIZE];
    const uint8_t *write_data = data;

    // If not page-aligned and not last chunk, this is an error
    bool is_last_chunk = (update_context.bytes_written + size == update_context.expected_size);

    if (size % FLASH_PAGE_SIZE != 0) {
        if (!is_last_chunk) {
            set_error("Chunk size must be 256-byte aligned (except last chunk)");
            return false;
        }

        // Last chunk - pad to page size
        write_size = FLASH_PAGE_ALIGN(size);
        memcpy(write_buffer, data, size);
        memset(write_buffer + size, 0xFF, write_size - size);  // Pad with 0xFF (erased flash)
        write_data = write_buffer;
    }

    // Update CRC32 with actual data (not padding)
    crc32_update(&update_context.crc_ctx, data, size);

    // Write to flash
    flash_op_result_t result = flash_write(update_context.current_offset, write_data,
                                            write_size, NULL, NULL);
    if (result != FLASH_OP_SUCCESS) {
        set_error("Flash write failed");
        return false;
    }

    // Update progress
    update_context.bytes_written += size;
    update_context.current_offset += write_size;

    printf("Wrote %lu bytes (%lu / %lu) - %lu%%\n",
           size, update_context.bytes_written, update_context.expected_size,
           (update_context.bytes_written * 100) / update_context.expected_size);

    return true;
}

bool firmware_manager_finalize_update(uint32_t final_crc32) {
    if (update_context.state != FIRMWARE_UPDATE_RECEIVING) {
        set_error("Not in receiving state");
        return false;
    }

    // Check all bytes received
    if (update_context.bytes_written != update_context.expected_size) {
        set_error("Incomplete firmware upload");
        return false;
    }

    update_context.state = FIRMWARE_UPDATE_VALIDATING;
    printf("Finalizing firmware update...\n");

    // Calculate final CRC32
    uint32_t calculated_crc32 = crc32_finalize(&update_context.crc_ctx);

    printf("CRC32: calculated=0x%08lx, expected=0x%08lx\n", calculated_crc32, final_crc32);

    // Verify CRC32
    if (calculated_crc32 != final_crc32) {
        set_error("CRC32 mismatch");
        metadata_mark_bank_invalid(update_context.target_bank);
        metadata_clear_update_in_progress();
        update_context.state = FIRMWARE_UPDATE_IDLE;
        return false;
    }

    // Double-check by reading from flash
    printf("Verifying firmware in flash...\n");
    uint32_t flash_crc32;
    flash_op_result_t result = flash_validate_firmware(update_context.target_bank,
                                                        final_crc32,
                                                        update_context.expected_size,
                                                        &flash_crc32,
                                                        NULL, NULL);
    if (result != FLASH_OP_SUCCESS) {
        set_error("Flash verification failed");
        metadata_mark_bank_invalid(update_context.target_bank);
        metadata_clear_update_in_progress();
        update_context.state = FIRMWARE_UPDATE_IDLE;
        return false;
    }

    // Mark bank as valid in metadata
    const char *version = (update_context.expected_version[0] != '\0') ?
                          update_context.expected_version : "uploaded";

    if (!metadata_mark_bank_valid(update_context.target_bank, final_crc32,
                                   update_context.expected_size, version)) {
        set_error("Failed to update metadata");
        update_context.state = FIRMWARE_UPDATE_IDLE;
        return false;
    }

    // Clear update in progress
    if (!metadata_clear_update_in_progress()) {
        printf("WARNING: Failed to clear update flag\n");
    }

    update_context.state = FIRMWARE_UPDATE_COMPLETE;
    printf("Firmware update finalized successfully!\n");
    printf("New firmware ready in bank %s\n",
           (update_context.target_bank == BANK_A) ? "A" : "B");
    printf("Call firmware_manager_activate_and_reboot() to activate\n");

    return true;
}

void firmware_manager_cancel_update(void) {
    printf("Cancelling firmware update...\n");

    if (update_context.state != FIRMWARE_UPDATE_IDLE) {
        // Mark target bank as invalid
        if (update_context.target_bank != BANK_UNKNOWN) {
            metadata_mark_bank_invalid(update_context.target_bank);
        }

        // Clear update in progress
        metadata_clear_update_in_progress();
    }

    // Reset state
    update_context.state = FIRMWARE_UPDATE_IDLE;
    update_context.bytes_written = 0;
    clear_error();

    printf("Update cancelled\n");
}

void __attribute__((noreturn)) firmware_manager_activate_and_reboot(void) {
    printf("Activating new firmware and rebooting...\n");

    // Set new active bank
    if (!metadata_set_active_bank(update_context.target_bank)) {
        printf("FATAL: Failed to set active bank\n");
        // Try to recover by rebooting with current firmware
        watchdog_reboot(0, 0, 0);
        while (1);  // Should never reach
    }

    printf("New firmware activated\n");
    printf("Rebooting in 2 seconds...\n");

    // Small delay to allow printf to complete
    sleep_ms(2000);

    // Trigger watchdog reboot
    watchdog_reboot(0, 0, 0);

    // Should never reach here
    while (1);
}

bool firmware_manager_rollback_and_reboot(void) {
    printf("Rolling back to previous firmware...\n");

    if (!metadata_trigger_rollback()) {
        printf("ERROR: Rollback failed (no valid backup firmware)\n");
        return false;
    }

    printf("Rollback successful\n");
    printf("Rebooting in 2 seconds...\n");

    sleep_ms(2000);

    watchdog_reboot(0, 0, 0);

    // Should never reach here
    while (1);
}

void firmware_manager_get_status(firmware_update_status_t *status) {
    if (status == NULL) {
        return;
    }

    status->state = update_context.state;
    status->bytes_received = update_context.bytes_written;
    status->total_bytes = update_context.expected_size;
    status->target_bank = update_context.target_bank;

    if (update_context.expected_size > 0) {
        status->progress_percent = (update_context.bytes_written * 100) / update_context.expected_size;
    } else {
        status->progress_percent = 0;
    }

    strncpy(status->error_message, update_context.error_message,
            sizeof(status->error_message) - 1);
    status->error_message[sizeof(status->error_message) - 1] = '\0';
}

uint32_t firmware_manager_get_progress(void) {
    if (update_context.expected_size == 0) {
        return 0;
    }

    return (update_context.bytes_written * 100) / update_context.expected_size;
}

bool firmware_manager_is_update_in_progress(void) {
    return (update_context.state != FIRMWARE_UPDATE_IDLE &&
            update_context.state != FIRMWARE_UPDATE_COMPLETE &&
            update_context.state != FIRMWARE_UPDATE_ERROR);
}
