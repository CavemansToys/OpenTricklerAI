#ifndef REST_FIRMWARE_H
#define REST_FIRMWARE_H

#include <stdbool.h>
#include "lwip/apps/fs.h"

/**
 * REST API Endpoints for Firmware Operations
 *
 * Endpoints:
 * - GET  /rest/firmware_status    - Get current firmware and update status
 * - GET  /rest/firmware_download  - Download firmware from URL
 * - POST /rest/firmware_activate  - Activate new firmware and reboot
 * - POST /rest/firmware_rollback  - Rollback to previous firmware and reboot
 * - POST /rest/firmware_cancel    - Cancel update in progress
 */

/**
 * Initialize firmware REST endpoints
 * Registers all endpoints with the REST handler
 *
 * @return true if initialization successful
 */
bool rest_firmware_init(void);

/**
 * GET /rest/firmware_status
 *
 * Returns JSON with:
 * - current_bank: "A" or "B"
 * - current_version: version string
 * - bank_a: {valid, size, crc32, version, boot_count}
 * - bank_b: {valid, size, crc32, version, boot_count}
 * - update_status: {state, progress, target_bank, error}
 * - rollback_occurred: bool
 */
bool http_rest_firmware_status(struct fs_file *file, int num_params,
                                char *params[], char *values[]);

/**
 * POST /rest/firmware_activate
 *
 * Activates uploaded firmware and triggers reboot
 *
 * WARNING: This function does not return!
 *
 * Returns: N/A (system reboots)
 */
bool http_rest_firmware_activate(struct fs_file *file, int num_params,
                                  char *params[], char *values[]);

/**
 * POST /rest/firmware_rollback
 *
 * Rolls back to previous firmware and triggers reboot
 *
 * WARNING: This function does not return if successful!
 *
 * Returns: Error message if rollback not possible
 */
bool http_rest_firmware_rollback(struct fs_file *file, int num_params,
                                  char *params[], char *values[]);

/**
 * POST /rest/firmware_cancel
 *
 * Cancels firmware update in progress
 *
 * Returns: Success/error message
 */
bool http_rest_firmware_cancel(struct fs_file *file, int num_params,
                                char *params[], char *values[]);

/**
 * GET /rest/firmware_download
 *
 * Downloads firmware from external HTTP URL
 *
 * Parameters:
 * - url: HTTP URL to download from
 * - crc32: Expected CRC32 (hex, optional)
 * - version: Expected version string (optional)
 *
 * Returns: Success/error message
 */
bool http_rest_firmware_download(struct fs_file *file, int num_params,
                                  char *params[], char *values[]);

#endif // REST_FIRMWARE_H
