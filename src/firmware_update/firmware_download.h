#ifndef FIRMWARE_DOWNLOAD_H
#define FIRMWARE_DOWNLOAD_H

#include <stdint.h>
#include <stdbool.h>

/**
 * HTTP URL Firmware Download
 *
 * Downloads firmware from external HTTP server and writes directly to flash.
 *
 * Features:
 * - Parses HTTP URLs
 * - Downloads via TCP connection
 * - Streams directly to flash (no buffering)
 * - Progress tracking
 * - Automatic CRC32 calculation
 *
 * Usage:
 * 1. Call firmware_download_init()
 * 2. Call firmware_download_start(url, expected_crc32)
 * 3. Monitor progress via firmware_download_get_progress()
 * 4. Check completion via firmware_download_is_complete()
 */

// Download state
typedef enum {
    DOWNLOAD_IDLE,
    DOWNLOAD_PARSING_URL,
    DOWNLOAD_RESOLVING_DNS,
    DOWNLOAD_CONNECTING,
    DOWNLOAD_SENDING_REQUEST,
    DOWNLOAD_RECEIVING_HEADERS,
    DOWNLOAD_RECEIVING_BODY,
    DOWNLOAD_VALIDATING,
    DOWNLOAD_COMPLETE,
    DOWNLOAD_ERROR
} firmware_download_state_t;

// Download status
typedef struct {
    firmware_download_state_t state;
    uint32_t bytes_downloaded;
    uint32_t total_bytes;
    uint32_t progress_percent;
    char error_message[128];
    char url[256];
} firmware_download_status_t;

/**
 * Initialize firmware download module
 *
 * @return true if initialization successful
 */
bool firmware_download_init(void);

/**
 * Start firmware download from URL
 *
 * @param url HTTP URL to download from (e.g., "http://example.com/firmware.bin")
 * @param expected_crc32 Expected CRC32 checksum (0 to skip validation)
 * @param expected_version Expected version string (NULL to skip)
 * @return true if download started successfully
 */
bool firmware_download_start(const char *url, uint32_t expected_crc32,
                              const char *expected_version);

/**
 * Cancel firmware download
 */
void firmware_download_cancel(void);

/**
 * Get download status
 *
 * @param status Output: Download status structure
 */
void firmware_download_get_status(firmware_download_status_t *status);

/**
 * Get download progress percentage (0-100)
 *
 * @return Progress percentage
 */
uint32_t firmware_download_get_progress(void);

/**
 * Check if download is in progress
 *
 * @return true if download in progress
 */
bool firmware_download_is_in_progress(void);

/**
 * Check if download is complete
 *
 * @return true if download complete
 */
bool firmware_download_is_complete(void);

/**
 * Check if download had an error
 *
 * @return true if error occurred
 */
bool firmware_download_has_error(void);

#endif // FIRMWARE_DOWNLOAD_H
