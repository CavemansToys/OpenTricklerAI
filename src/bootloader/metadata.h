#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>
#include <stdbool.h>
#include "flash_partitions.h"

/**
 * Firmware Metadata Management for Dual-Bank OTA System
 *
 * Features:
 * - Double-buffered metadata sectors for atomic updates
 * - Sequence numbers to determine active metadata
 * - CRC32 validation for metadata integrity
 * - Boot counting for automatic rollback
 * - Firmware validation status per bank
 */

// Metadata magic number "OTMU" (OpenTrickler Metadata Update)
#define METADATA_MAGIC          0x4F544D55

// Metadata structure version (increment on structure changes)
#define METADATA_VERSION        1

// Maximum boot attempts before rollback
#define MAX_BOOT_ATTEMPTS       3

// Firmware version string length
#define VERSION_STRING_LENGTH   32

// Bank status flags
#define BANK_VALID              0xFF
#define BANK_INVALID            0x00

// Update status flags
#define UPDATE_IN_PROGRESS      0xFF
#define UPDATE_IDLE             0x00

/**
 * Firmware Metadata Structure
 *
 * Stored in both METADATA_SECTOR_A and METADATA_SECTOR_B.
 * The sector with the higher sequence number is considered active.
 * This provides atomic updates - new metadata written to inactive sector,
 * only becomes active after successful write and validation.
 *
 * Total size: Designed to fit within single 4KB sector with room for expansion
 */
typedef struct __attribute__((packed)) {
    // Header
    uint32_t magic;                     // METADATA_MAGIC for validation
    uint32_t version;                   // Metadata structure version
    uint32_t sequence;                  // Incremented on each write (for double-buffering)

    // Active bank selection
    firmware_bank_t active_bank;        // Which bank to boot from (BANK_A or BANK_B)
    uint8_t padding1[3];                // Align to 4-byte boundary

    // Bank A status
    uint32_t bank_a_crc32;              // CRC32 of entire firmware image
    uint32_t bank_a_size;               // Firmware size in bytes
    uint8_t  bank_a_version[VERSION_STRING_LENGTH];  // Version string (null-terminated)
    uint8_t  bank_a_boot_count;         // Number of boot attempts
    uint8_t  bank_a_valid;              // BANK_VALID or BANK_INVALID
    uint8_t  padding2[2];               // Align to 4-byte boundary

    // Bank B status
    uint32_t bank_b_crc32;              // CRC32 of entire firmware image
    uint32_t bank_b_size;               // Firmware size in bytes
    uint8_t  bank_b_version[VERSION_STRING_LENGTH];  // Version string (null-terminated)
    uint8_t  bank_b_boot_count;         // Number of boot attempts
    uint8_t  bank_b_valid;              // BANK_VALID or BANK_INVALID
    uint8_t  padding3[2];               // Align to 4-byte boundary

    // Update state
    uint8_t  update_in_progress;        // UPDATE_IN_PROGRESS or UPDATE_IDLE
    firmware_bank_t update_target;      // Target bank for current update
    uint8_t  padding4[2];               // Align to 4-byte boundary

    // Rollback tracking
    uint8_t  rollback_occurred;         // 0xFF if last boot was a rollback
    uint8_t  rollback_count;            // Number of times rollback has occurred
    uint8_t  padding5[2];               // Align to 4-byte boundary

    // Reserved for future expansion
    uint8_t  reserved[128];

    // Checksum (must be last field)
    uint32_t metadata_crc32;            // CRC32 of entire structure (excluding this field)

} firmware_metadata_t;

// Ensure metadata fits in one sector
_Static_assert(sizeof(firmware_metadata_t) <= METADATA_SECTOR_SIZE,
               "Metadata structure too large for sector");

/**
 * Metadata Operations
 */

/**
 * Initialize metadata system
 * Reads both metadata sectors and determines which is active
 *
 * @return true if metadata valid and loaded, false if corrupted/missing
 */
bool metadata_init(void);

/**
 * Read metadata from flash
 * Automatically selects the valid metadata sector with highest sequence number
 *
 * @param meta Pointer to metadata structure to fill
 * @return true if valid metadata found, false otherwise
 */
bool metadata_read(firmware_metadata_t *meta);

/**
 * Write metadata to flash (atomic operation)
 * Writes to inactive sector first, increments sequence number
 *
 * @param meta Pointer to metadata structure to write
 * @return true if write successful, false otherwise
 */
bool metadata_write(const firmware_metadata_t *meta);

/**
 * Get current metadata
 *
 * @param meta Pointer to metadata structure to fill
 * @return true if metadata retrieved, false otherwise
 */
bool metadata_get_current(firmware_metadata_t *meta);

/**
 * Update active bank in metadata and write atomically
 *
 * @param new_bank New active bank (BANK_A or BANK_B)
 * @return true if update successful, false otherwise
 */
bool metadata_set_active_bank(firmware_bank_t new_bank);

/**
 * Increment boot counter for active bank
 *
 * @return true if increment successful, false otherwise
 */
bool metadata_increment_boot_count(void);

/**
 * Reset boot counter for active bank (called after successful boot confirmation)
 *
 * @return true if reset successful, false otherwise
 */
bool metadata_reset_boot_count(void);

/**
 * Mark firmware bank as valid
 *
 * @param bank Bank to mark as valid
 * @param crc32 CRC32 checksum of firmware
 * @param size Size of firmware in bytes
 * @param version Version string (null-terminated, max VERSION_STRING_LENGTH)
 * @return true if update successful, false otherwise
 */
bool metadata_mark_bank_valid(firmware_bank_t bank, uint32_t crc32,
                               uint32_t size, const char *version);

/**
 * Mark firmware bank as invalid
 *
 * @param bank Bank to mark as invalid
 * @return true if update successful, false otherwise
 */
bool metadata_mark_bank_invalid(firmware_bank_t bank);

/**
 * Set update in progress flag
 *
 * @param target_bank Target bank for update
 * @return true if update successful, false otherwise
 */
bool metadata_set_update_in_progress(firmware_bank_t target_bank);

/**
 * Clear update in progress flag
 *
 * @return true if update successful, false otherwise
 */
bool metadata_clear_update_in_progress(void);

/**
 * Trigger rollback to opposite bank
 *
 * @return true if rollback successful, false otherwise
 */
bool metadata_trigger_rollback(void);

/**
 * Check if rollback occurred on last boot
 *
 * @return true if rollback occurred, false otherwise
 */
bool metadata_did_rollback_occur(void);

/**
 * Clear rollback flag (after user acknowledgment)
 *
 * @return true if clear successful, false otherwise
 */
bool metadata_clear_rollback_flag(void);

/**
 * Get bank information
 *
 * @param bank Bank to query
 * @param crc32 Output: CRC32 checksum (can be NULL)
 * @param size Output: Size in bytes (can be NULL)
 * @param version Output: Version string (can be NULL, must be VERSION_STRING_LENGTH)
 * @param valid Output: Valid flag (can be NULL)
 * @param boot_count Output: Boot count (can be NULL)
 * @return true if bank info retrieved, false otherwise
 */
bool metadata_get_bank_info(firmware_bank_t bank, uint32_t *crc32,
                             uint32_t *size, char *version,
                             uint8_t *valid, uint8_t *boot_count);

/**
 * Validate metadata structure integrity
 *
 * @param meta Pointer to metadata structure
 * @return true if metadata valid, false if corrupted
 */
bool metadata_validate(const firmware_metadata_t *meta);

/**
 * Calculate metadata CRC32 (excluding the crc32 field itself)
 *
 * @param meta Pointer to metadata structure
 * @return CRC32 checksum
 */
uint32_t metadata_calculate_crc32(const firmware_metadata_t *meta);

/**
 * Initialize metadata to factory defaults
 *
 * @param meta Pointer to metadata structure to initialize
 * @param initial_bank Initial active bank (usually BANK_A)
 */
void metadata_init_defaults(firmware_metadata_t *meta, firmware_bank_t initial_bank);

#endif // METADATA_H
