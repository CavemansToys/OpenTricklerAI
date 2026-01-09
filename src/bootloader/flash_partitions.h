#ifndef FLASH_PARTITIONS_H
#define FLASH_PARTITIONS_H

#include <stdint.h>

/**
 * Flash Memory Layout for Dual-Bank OTA Firmware Update System
 *
 * Total Flash: 2MB (2,097,152 bytes)
 * Base Address: 0x10000000 (XIP - Execute In Place region)
 *
 * Layout:
 * 0x10000000-0x100000FF  256 B     Boot2 (Pico SDK bootloader)
 * 0x10000100-0x10003FFF  ~16 KB    Custom Bootloader
 * 0x10004000-0x10004FFF  4 KB      Metadata Sector A (Primary)
 * 0x10005000-0x10005FFF  4 KB      Metadata Sector B (Backup)
 * 0x10006000-0x100E5FFF  896 KB    Firmware Bank A
 * 0x100E6000-0x101C5FFF  896 KB    Firmware Bank B
 * 0x101C6000-0x101FFFFF  232 KB    Reserved (Future expansion)
 */

// Flash base address (XIP region)
#define FLASH_BASE_ADDRESS          0x10000000

// Flash characteristics
#define FLASH_TOTAL_SIZE            (2 * 1024 * 1024)  // 2MB
#define FLASH_SECTOR_SIZE           4096               // 4KB erase sector
#define FLASH_PAGE_SIZE             256                // 256 byte write page

// Boot2 (Pico SDK second-stage bootloader)
#define BOOT2_ADDRESS               0x10000000
#define BOOT2_SIZE                  256

// Custom Bootloader
#define BOOTLOADER_ADDRESS          0x10000100
#define BOOTLOADER_SIZE             0x3F00             // ~16KB (16,128 bytes)
#define BOOTLOADER_END              (BOOTLOADER_ADDRESS + BOOTLOADER_SIZE)

// Metadata Sectors (Double-buffered for atomic updates)
#define METADATA_SECTOR_A_ADDRESS   0x10004000
#define METADATA_SECTOR_B_ADDRESS   0x10005000
#define METADATA_SECTOR_SIZE        FLASH_SECTOR_SIZE  // 4KB

// Firmware Bank A
#define BANK_A_ADDRESS              0x10006000
#define BANK_A_SIZE                 (896 * 1024)       // 896KB (917,504 bytes)
#define BANK_A_END                  (BANK_A_ADDRESS + BANK_A_SIZE)

// Firmware Bank B
#define BANK_B_ADDRESS              0x100E6000
#define BANK_B_SIZE                 (896 * 1024)       // 896KB (917,504 bytes)
#define BANK_B_END                  (BANK_B_ADDRESS + BANK_B_SIZE)

// Reserved space
#define RESERVED_ADDRESS            0x101C6000
#define RESERVED_SIZE               (232 * 1024)       // 232KB

// Flash offsets (relative to FLASH_BASE_ADDRESS for flash_range_* functions)
#define BOOTLOADER_OFFSET           (BOOTLOADER_ADDRESS - FLASH_BASE_ADDRESS)
#define METADATA_SECTOR_A_OFFSET    (METADATA_SECTOR_A_ADDRESS - FLASH_BASE_ADDRESS)
#define METADATA_SECTOR_B_OFFSET    (METADATA_SECTOR_B_ADDRESS - FLASH_BASE_ADDRESS)
#define BANK_A_OFFSET               (BANK_A_ADDRESS - FLASH_BASE_ADDRESS)
#define BANK_B_OFFSET               (BANK_B_ADDRESS - FLASH_BASE_ADDRESS)

// Number of sectors in each bank
#define BANK_SECTOR_COUNT           (BANK_A_SIZE / FLASH_SECTOR_SIZE)  // 224 sectors

// Alignment helpers
#define FLASH_SECTOR_ALIGN(x)       (((x) + (FLASH_SECTOR_SIZE - 1)) & ~(FLASH_SECTOR_SIZE - 1))
#define FLASH_PAGE_ALIGN(x)         (((x) + (FLASH_PAGE_SIZE - 1)) & ~(FLASH_PAGE_SIZE - 1))
#define IS_SECTOR_ALIGNED(x)        (((x) & (FLASH_SECTOR_SIZE - 1)) == 0)
#define IS_PAGE_ALIGNED(x)          (((x) & (FLASH_PAGE_SIZE - 1)) == 0)

// Firmware bank enumeration
typedef enum {
    BANK_A = 0,
    BANK_B = 1,
    BANK_UNKNOWN = 0xFF
} firmware_bank_t;

// Helper functions (inline for bootloader and application use)
static inline uint32_t bank_get_address(firmware_bank_t bank) {
    return (bank == BANK_A) ? BANK_A_ADDRESS :
           (bank == BANK_B) ? BANK_B_ADDRESS : 0;
}

static inline uint32_t bank_get_offset(firmware_bank_t bank) {
    return (bank == BANK_A) ? BANK_A_OFFSET :
           (bank == BANK_B) ? BANK_B_OFFSET : 0;
}

static inline uint32_t bank_get_size(firmware_bank_t bank) {
    return (bank == BANK_A || bank == BANK_B) ? BANK_A_SIZE : 0;
}

static inline firmware_bank_t bank_get_opposite(firmware_bank_t bank) {
    return (bank == BANK_A) ? BANK_B :
           (bank == BANK_B) ? BANK_A : BANK_UNKNOWN;
}

// Metadata sector helper
static inline uint32_t metadata_get_address(int sector) {
    return (sector == 0) ? METADATA_SECTOR_A_ADDRESS : METADATA_SECTOR_B_ADDRESS;
}

static inline uint32_t metadata_get_offset(int sector) {
    return (sector == 0) ? METADATA_SECTOR_A_OFFSET : METADATA_SECTOR_B_OFFSET;
}

#endif // FLASH_PARTITIONS_H
