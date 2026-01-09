#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

/**
 * CRC32 Calculation for Firmware Validation
 *
 * Uses standard CRC32 algorithm (polynomial 0x04C11DB7)
 * Compatible with ZIP, PNG, Ethernet, etc.
 *
 * Features:
 * - Table-based lookup for speed
 * - Incremental calculation for large files
 * - Small memory footprint
 */

// CRC32 polynomial (standard)
#define CRC32_POLYNOMIAL    0xEDB88320  // Reversed 0x04C11DB7

// Initial CRC32 value
#define CRC32_INIT          0xFFFFFFFF

/**
 * CRC32 context for incremental calculation
 */
typedef struct {
    uint32_t crc;       // Current CRC value
    uint32_t total;     // Total bytes processed
} crc32_context_t;

/**
 * Initialize CRC32 lookup table
 * Must be called once before using CRC32 functions
 */
void crc32_init(void);

/**
 * Calculate CRC32 of a buffer (single-shot)
 *
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @return CRC32 checksum
 */
uint32_t crc32_calculate(const uint8_t *data, size_t length);

/**
 * Initialize CRC32 context for incremental calculation
 *
 * @param ctx Pointer to CRC32 context
 */
void crc32_begin(crc32_context_t *ctx);

/**
 * Update CRC32 with additional data
 *
 * @param ctx Pointer to CRC32 context
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 */
void crc32_update(crc32_context_t *ctx, const uint8_t *data, size_t length);

/**
 * Finalize CRC32 calculation
 *
 * @param ctx Pointer to CRC32 context
 * @return Final CRC32 checksum
 */
uint32_t crc32_finalize(crc32_context_t *ctx);

/**
 * Get current CRC32 value (without finalizing)
 *
 * @param ctx Pointer to CRC32 context
 * @return Current CRC32 value
 */
uint32_t crc32_get_current(const crc32_context_t *ctx);

/**
 * Get total bytes processed
 *
 * @param ctx Pointer to CRC32 context
 * @return Total bytes processed
 */
uint32_t crc32_get_total_bytes(const crc32_context_t *ctx);

#endif // CRC32_H
