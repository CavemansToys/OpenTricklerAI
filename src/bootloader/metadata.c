#include "metadata.h"
#include "../firmware_update/crc32.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdio.h>

/**
 * Metadata Implementation with Double-Buffering
 *
 * Two metadata sectors provide atomic updates:
 * 1. Read both sectors
 * 2. Select sector with highest sequence number (and valid CRC)
 * 3. To write: write to inactive sector, increment sequence
 * 4. New metadata becomes active only after successful write
 *
 * This ensures power loss during write never corrupts the only valid metadata.
 */

// Current metadata cache
static firmware_metadata_t current_metadata;
static bool metadata_loaded = false;

// Helper: Read metadata from specific sector
static bool read_metadata_sector(int sector, firmware_metadata_t *meta) {
    if (meta == NULL || (sector != 0 && sector != 1)) {
        return false;
    }

    uint32_t address = metadata_get_address(sector);
    const firmware_metadata_t *flash_meta = (const firmware_metadata_t *)address;

    // Copy from flash to RAM
    memcpy(meta, flash_meta, sizeof(firmware_metadata_t));

    return true;
}

// Helper: Write metadata to specific sector
static bool write_metadata_sector(int sector, const firmware_metadata_t *meta) {
    if (meta == NULL || (sector != 0 && sector != 1)) {
        return false;
    }

    uint32_t offset = metadata_get_offset(sector);

    // Erase sector (must disable interrupts)
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    // Write metadata in 256-byte pages
    const uint8_t *data = (const uint8_t *)meta;
    size_t remaining = sizeof(firmware_metadata_t);
    uint32_t write_offset = offset;

    while (remaining > 0) {
        size_t chunk_size = (remaining < FLASH_PAGE_SIZE) ? remaining : FLASH_PAGE_SIZE;

        // Prepare aligned write buffer
        uint8_t write_buffer[FLASH_PAGE_SIZE];
        memcpy(write_buffer, data, chunk_size);
        if (chunk_size < FLASH_PAGE_SIZE) {
            memset(write_buffer + chunk_size, 0xFF, FLASH_PAGE_SIZE - chunk_size);
        }

        // Write page (must disable interrupts)
        ints = save_and_disable_interrupts();
        flash_range_program(write_offset, write_buffer, FLASH_PAGE_SIZE);
        restore_interrupts(ints);

        data += chunk_size;
        write_offset += FLASH_PAGE_SIZE;
        remaining -= chunk_size;
    }

    return true;
}

uint32_t metadata_calculate_crc32(const firmware_metadata_t *meta) {
    if (meta == NULL) {
        return 0;
    }

    // Calculate CRC32 of everything except the crc32 field itself
    size_t data_size = sizeof(firmware_metadata_t) - sizeof(uint32_t);
    return crc32_calculate((const uint8_t *)meta, data_size);
}

bool metadata_validate(const firmware_metadata_t *meta) {
    if (meta == NULL) {
        return false;
    }

    // Check magic number
    if (meta->magic != METADATA_MAGIC) {
        return false;
    }

    // Check version
    if (meta->version != METADATA_VERSION) {
        return false;
    }

    // Validate CRC32
    uint32_t calculated_crc = metadata_calculate_crc32(meta);
    if (calculated_crc != meta->metadata_crc32) {
        return false;
    }

    // Check active bank is valid
    if (meta->active_bank != BANK_A && meta->active_bank != BANK_B) {
        return false;
    }

    return true;
}

void metadata_init_defaults(firmware_metadata_t *meta, firmware_bank_t initial_bank) {
    if (meta == NULL) {
        return;
    }

    memset(meta, 0, sizeof(firmware_metadata_t));

    // Set header
    meta->magic = METADATA_MAGIC;
    meta->version = METADATA_VERSION;
    meta->sequence = 1;

    // Set active bank
    meta->active_bank = initial_bank;

    // Mark Bank A as valid (factory firmware)
    meta->bank_a_valid = BANK_VALID;
    meta->bank_a_boot_count = 0;
    meta->bank_a_size = 0;  // Unknown initially
    meta->bank_a_crc32 = 0; // Unknown initially
    strncpy((char *)meta->bank_a_version, "factory", VERSION_STRING_LENGTH - 1);

    // Mark Bank B as invalid initially
    meta->bank_b_valid = BANK_INVALID;
    meta->bank_b_boot_count = 0;

    // Clear update state
    meta->update_in_progress = UPDATE_IDLE;
    meta->update_target = BANK_UNKNOWN;

    // Clear rollback flags
    meta->rollback_occurred = 0x00;
    meta->rollback_count = 0;

    // Calculate CRC32
    meta->metadata_crc32 = metadata_calculate_crc32(meta);
}

bool metadata_init(void) {
    // Initialize CRC32 table
    crc32_init();

    firmware_metadata_t meta_a, meta_b;
    bool valid_a = false, valid_b = false;

    // Read both metadata sectors
    if (read_metadata_sector(0, &meta_a)) {
        valid_a = metadata_validate(&meta_a);
    }

    if (read_metadata_sector(1, &meta_b)) {
        valid_b = metadata_validate(&meta_b);
    }

    // Select metadata based on validity and sequence
    if (valid_a && valid_b) {
        // Both valid, select higher sequence
        if (meta_a.sequence > meta_b.sequence) {
            memcpy(&current_metadata, &meta_a, sizeof(firmware_metadata_t));
        } else {
            memcpy(&current_metadata, &meta_b, sizeof(firmware_metadata_t));
        }
        metadata_loaded = true;
    } else if (valid_a) {
        memcpy(&current_metadata, &meta_a, sizeof(firmware_metadata_t));
        metadata_loaded = true;
    } else if (valid_b) {
        memcpy(&current_metadata, &meta_b, sizeof(firmware_metadata_t));
        metadata_loaded = true;
    } else {
        // No valid metadata found - initialize to defaults
        metadata_init_defaults(&current_metadata, BANK_A);

        // Write to both sectors
        write_metadata_sector(0, &current_metadata);
        current_metadata.sequence++;
        current_metadata.metadata_crc32 = metadata_calculate_crc32(&current_metadata);
        write_metadata_sector(1, &current_metadata);

        metadata_loaded = true;
        printf("WARNING: No valid metadata found, initialized to defaults\n");
    }

    return metadata_loaded;
}

bool metadata_read(firmware_metadata_t *meta) {
    if (!metadata_loaded) {
        if (!metadata_init()) {
            return false;
        }
    }

    if (meta != NULL) {
        memcpy(meta, &current_metadata, sizeof(firmware_metadata_t));
    }

    return true;
}

bool metadata_write(const firmware_metadata_t *meta) {
    if (meta == NULL) {
        return false;
    }

    // Create working copy
    firmware_metadata_t write_meta;
    memcpy(&write_meta, meta, sizeof(firmware_metadata_t));

    // Increment sequence number
    write_meta.sequence = current_metadata.sequence + 1;

    // Recalculate CRC32
    write_meta.metadata_crc32 = metadata_calculate_crc32(&write_meta);

    // Determine which sector is currently active
    firmware_metadata_t meta_a, meta_b;
    read_metadata_sector(0, &meta_a);
    read_metadata_sector(1, &meta_b);

    int inactive_sector;
    if (metadata_validate(&meta_a) && metadata_validate(&meta_b)) {
        // Both valid, write to sector with lower sequence
        inactive_sector = (meta_a.sequence < meta_b.sequence) ? 0 : 1;
    } else if (metadata_validate(&meta_a)) {
        inactive_sector = 1;
    } else {
        inactive_sector = 0;
    }

    // Write to inactive sector
    if (!write_metadata_sector(inactive_sector, &write_meta)) {
        return false;
    }

    // Verify write
    firmware_metadata_t verify_meta;
    read_metadata_sector(inactive_sector, &verify_meta);
    if (!metadata_validate(&verify_meta)) {
        printf("ERROR: Metadata write verification failed\n");
        return false;
    }

    // Update cached metadata
    memcpy(&current_metadata, &write_meta, sizeof(firmware_metadata_t));

    return true;
}

bool metadata_get_current(firmware_metadata_t *meta) {
    return metadata_read(meta);
}

bool metadata_set_active_bank(firmware_bank_t new_bank) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    if (new_bank != BANK_A && new_bank != BANK_B) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    meta.active_bank = new_bank;

    return metadata_write(&meta);
}

bool metadata_increment_boot_count(void) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    if (meta.active_bank == BANK_A) {
        meta.bank_a_boot_count++;
    } else if (meta.active_bank == BANK_B) {
        meta.bank_b_boot_count++;
    } else {
        return false;
    }

    return metadata_write(&meta);
}

bool metadata_reset_boot_count(void) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    if (meta.active_bank == BANK_A) {
        meta.bank_a_boot_count = 0;
    } else if (meta.active_bank == BANK_B) {
        meta.bank_b_boot_count = 0;
    } else {
        return false;
    }

    return metadata_write(&meta);
}

bool metadata_mark_bank_valid(firmware_bank_t bank, uint32_t crc32,
                               uint32_t size, const char *version) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    if (bank != BANK_A && bank != BANK_B) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    if (bank == BANK_A) {
        meta.bank_a_crc32 = crc32;
        meta.bank_a_size = size;
        meta.bank_a_valid = BANK_VALID;
        meta.bank_a_boot_count = 0;
        if (version != NULL) {
            strncpy((char *)meta.bank_a_version, version, VERSION_STRING_LENGTH - 1);
            meta.bank_a_version[VERSION_STRING_LENGTH - 1] = '\0';
        }
    } else {
        meta.bank_b_crc32 = crc32;
        meta.bank_b_size = size;
        meta.bank_b_valid = BANK_VALID;
        meta.bank_b_boot_count = 0;
        if (version != NULL) {
            strncpy((char *)meta.bank_b_version, version, VERSION_STRING_LENGTH - 1);
            meta.bank_b_version[VERSION_STRING_LENGTH - 1] = '\0';
        }
    }

    return metadata_write(&meta);
}

bool metadata_mark_bank_invalid(firmware_bank_t bank) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    if (bank != BANK_A && bank != BANK_B) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    if (bank == BANK_A) {
        meta.bank_a_valid = BANK_INVALID;
        meta.bank_a_boot_count = MAX_BOOT_ATTEMPTS;  // Prevent re-use
    } else {
        meta.bank_b_valid = BANK_INVALID;
        meta.bank_b_boot_count = MAX_BOOT_ATTEMPTS;  // Prevent re-use
    }

    return metadata_write(&meta);
}

bool metadata_set_update_in_progress(firmware_bank_t target_bank) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    if (target_bank != BANK_A && target_bank != BANK_B) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    meta.update_in_progress = UPDATE_IN_PROGRESS;
    meta.update_target = target_bank;

    return metadata_write(&meta);
}

bool metadata_clear_update_in_progress(void) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    meta.update_in_progress = UPDATE_IDLE;
    meta.update_target = BANK_UNKNOWN;

    return metadata_write(&meta);
}

bool metadata_trigger_rollback(void) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    // Switch to opposite bank
    firmware_bank_t new_bank = bank_get_opposite(meta.active_bank);

    // Check if opposite bank is valid
    bool opposite_valid = false;
    if (new_bank == BANK_A) {
        opposite_valid = (meta.bank_a_valid == BANK_VALID);
    } else if (new_bank == BANK_B) {
        opposite_valid = (meta.bank_b_valid == BANK_VALID);
    }

    if (!opposite_valid) {
        printf("ERROR: Cannot rollback, opposite bank is invalid\n");
        return false;
    }

    // Mark current bank as invalid
    if (meta.active_bank == BANK_A) {
        meta.bank_a_valid = BANK_INVALID;
        meta.bank_a_boot_count = MAX_BOOT_ATTEMPTS;
    } else {
        meta.bank_b_valid = BANK_INVALID;
        meta.bank_b_boot_count = MAX_BOOT_ATTEMPTS;
    }

    // Switch active bank
    meta.active_bank = new_bank;

    // Reset boot count for new active bank
    if (new_bank == BANK_A) {
        meta.bank_a_boot_count = 0;
    } else {
        meta.bank_b_boot_count = 0;
    }

    // Set rollback flag
    meta.rollback_occurred = 0xFF;
    meta.rollback_count++;

    printf("ROLLBACK: Switching to bank %s\n", (new_bank == BANK_A) ? "A" : "B");

    return metadata_write(&meta);
}

bool metadata_did_rollback_occur(void) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    return (current_metadata.rollback_occurred == 0xFF);
}

bool metadata_clear_rollback_flag(void) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    firmware_metadata_t meta;
    memcpy(&meta, &current_metadata, sizeof(firmware_metadata_t));

    meta.rollback_occurred = 0x00;

    return metadata_write(&meta);
}

bool metadata_get_bank_info(firmware_bank_t bank, uint32_t *crc32,
                             uint32_t *size, char *version,
                             uint8_t *valid, uint8_t *boot_count) {
    if (!metadata_loaded && !metadata_init()) {
        return false;
    }

    if (bank != BANK_A && bank != BANK_B) {
        return false;
    }

    if (bank == BANK_A) {
        if (crc32) *crc32 = current_metadata.bank_a_crc32;
        if (size) *size = current_metadata.bank_a_size;
        if (version) {
            strncpy(version, (const char *)current_metadata.bank_a_version, VERSION_STRING_LENGTH);
        }
        if (valid) *valid = current_metadata.bank_a_valid;
        if (boot_count) *boot_count = current_metadata.bank_a_boot_count;
    } else {
        if (crc32) *crc32 = current_metadata.bank_b_crc32;
        if (size) *size = current_metadata.bank_b_size;
        if (version) {
            strncpy(version, (const char *)current_metadata.bank_b_version, VERSION_STRING_LENGTH);
        }
        if (valid) *valid = current_metadata.bank_b_valid;
        if (boot_count) *boot_count = current_metadata.bank_b_boot_count;
    }

    return true;
}
