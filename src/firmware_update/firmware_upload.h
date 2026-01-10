#ifndef FIRMWARE_UPLOAD_H
#define FIRMWARE_UPLOAD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HTTP POST Firmware Upload Handler
 *
 * Integrates with lwIP's httpd module to handle firmware uploads via HTTP POST.
 *
 * Usage:
 * 1. Call firmware_upload_init() during application startup
 * 2. POST firmware binary to /upload
 * 3. lwIP callbacks will handle the upload automatically
 * 4. Check status via /rest/firmware_status
 * 5. Activate via /rest/firmware_activate
 *
 * POST Format:
 * - Content-Type: application/octet-stream or multipart/form-data
 * - Custom headers: X-Firmware-Size, X-Firmware-CRC32, X-Firmware-Version (optional)
 * - Body: Raw firmware binary
 */

/**
 * Initialize firmware upload handler
 * Registers POST handler with lwIP httpd
 *
 * @return true if initialization successful
 */
bool firmware_upload_init(void);

/**
 * Get upload handler URI
 *
 * @return URI string for firmware upload endpoint
 */
const char *firmware_upload_get_uri(void);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_UPLOAD_H
