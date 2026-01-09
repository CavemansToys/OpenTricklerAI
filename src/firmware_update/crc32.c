#include "crc32.h"
#include <string.h>

/**
 * CRC32 Implementation with Table Lookup
 *
 * This implementation uses a 256-entry lookup table for fast CRC32 calculation.
 * The table is generated once at initialization based on the CRC32 polynomial.
 *
 * Algorithm: Standard CRC32 (used in ZIP, PNG, Ethernet, etc.)
 * Polynomial: 0x04C11DB7 (reversed: 0xEDB88320)
 */

// CRC32 lookup table (256 entries)
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

/**
 * Generate CRC32 lookup table
 */
static void crc32_generate_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

void crc32_init(void) {
    if (!crc32_table_initialized) {
        crc32_generate_table();
    }
}

uint32_t crc32_calculate(const uint8_t *data, size_t length) {
    // Ensure table is initialized
    if (!crc32_table_initialized) {
        crc32_init();
    }

    // Handle NULL pointer
    if (data == NULL) {
        return 0;
    }

    // Calculate CRC32
    uint32_t crc = CRC32_INIT;
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
    }

    // Final XOR
    return crc ^ 0xFFFFFFFF;
}

void crc32_begin(crc32_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    // Ensure table is initialized
    if (!crc32_table_initialized) {
        crc32_init();
    }

    ctx->crc = CRC32_INIT;
    ctx->total = 0;
}

void crc32_update(crc32_context_t *ctx, const uint8_t *data, size_t length) {
    if (ctx == NULL || data == NULL) {
        return;
    }

    // Update CRC
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (ctx->crc ^ data[i]) & 0xFF;
        ctx->crc = (ctx->crc >> 8) ^ crc32_table[index];
    }

    // Track total bytes
    ctx->total += length;
}

uint32_t crc32_finalize(crc32_context_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }

    // Final XOR
    return ctx->crc ^ 0xFFFFFFFF;
}

uint32_t crc32_get_current(const crc32_context_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }

    // Return current CRC (with final XOR)
    return ctx->crc ^ 0xFFFFFFFF;
}

uint32_t crc32_get_total_bytes(const crc32_context_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }

    return ctx->total;
}
