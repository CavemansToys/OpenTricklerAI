#include "firmware_upload.h"
#include "firmware_manager.h"
#include "lwip/apps/httpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * HTTP POST Firmware Upload Implementation
 *
 * Implements lwIP httpd POST callbacks for firmware upload
 */

// Upload URI
#define FIRMWARE_UPLOAD_URI "/upload"

// POST connection state
typedef struct {
    uint32_t expected_size;
    uint32_t expected_crc32;
    char expected_version[32];
    bool upload_started;
    bool upload_complete;
    bool upload_error;
    char error_message[128];
} upload_connection_state_t;

/**
 * lwIP POST callback: Begin POST request
 *
 * Called when POST request starts
 */
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd) {

    printf("POST begin: uri=%s, content_len=%d\n", uri, content_len);

    // Check if this is a firmware upload request
    if (strcmp(uri, FIRMWARE_UPLOAD_URI) != 0) {
        return ERR_ARG;  // Not our URI
    }

    // Allocate connection state
    upload_connection_state_t *state = (upload_connection_state_t *)mem_malloc(sizeof(upload_connection_state_t));
    if (state == NULL) {
        printf("ERROR: Failed to allocate upload state\n");
        return ERR_MEM;
    }

    memset(state, 0, sizeof(upload_connection_state_t));

    // Parse headers for firmware metadata
    // Look for X-Firmware-Size, X-Firmware-CRC32, X-Firmware-Version
    const char *size_header = strstr(http_request, "X-Firmware-Size:");
    const char *crc_header = strstr(http_request, "X-Firmware-CRC32:");
    const char *version_header = strstr(http_request, "X-Firmware-Version:");

    if (size_header != NULL) {
        state->expected_size = strtoul(size_header + 16, NULL, 10);
        printf("Expected size: %lu bytes\n", state->expected_size);
    } else {
        // Use content-length if no custom header
        state->expected_size = content_len;
    }

    if (crc_header != NULL) {
        state->expected_crc32 = strtoul(crc_header + 17, NULL, 16);
        printf("Expected CRC32: 0x%08lx\n", state->expected_crc32);
    }

    if (version_header != NULL) {
        const char *version_start = version_header + 19;
        const char *version_end = strstr(version_start, "\r\n");
        if (version_end != NULL) {
            size_t version_len = version_end - version_start;
            if (version_len > 0 && version_len < sizeof(state->expected_version)) {
                memcpy(state->expected_version, version_start, version_len);
                state->expected_version[version_len] = '\0';
                printf("Expected version: %s\n", state->expected_version);
            }
        }
    }

    // Validate parameters
    if (state->expected_size == 0) {
        snprintf(state->error_message, sizeof(state->error_message),
                 "Missing or invalid firmware size");
        state->upload_error = true;
        *(void **)connection = state;
        return ERR_OK;  // Return OK to allow error response
    }

    // Start firmware update
    const char *version = (state->expected_version[0] != '\0') ? state->expected_version : NULL;
    if (!firmware_manager_start_update(state->expected_size, version)) {
        snprintf(state->error_message, sizeof(state->error_message),
                 "Failed to start firmware update");
        state->upload_error = true;
    } else {
        state->upload_started = true;
    }

    // Enable manual window update for flow control
    *post_auto_wnd = 0;

    // Save state in connection
    *(void **)connection = state;

    printf("POST begin complete\n");
    return ERR_OK;
}

/**
 * lwIP POST callback: Receive POST data
 *
 * Called when data chunks arrive
 */
err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    upload_connection_state_t *state = (upload_connection_state_t *)*(void **)connection;

    if (state == NULL) {
        printf("ERROR: No upload state\n");
        return ERR_ARG;
    }

    // Skip if already in error state
    if (state->upload_error) {
        pbuf_free(p);
        return ERR_OK;
    }

    // Process each pbuf in the chain
    struct pbuf *q = p;
    while (q != NULL) {
        // Write chunk to firmware manager
        if (!firmware_manager_write_chunk((const uint8_t *)q->payload, q->len)) {
            snprintf(state->error_message, sizeof(state->error_message),
                     "Failed to write firmware chunk");
            state->upload_error = true;
            firmware_manager_cancel_update();
            pbuf_free(p);
            return ERR_OK;
        }

        q = q->next;
    }

    // Free the pbuf
    pbuf_free(p);

    // Acknowledge data received (manual flow control)
    httpd_post_data_recved(connection, p->tot_len);

    return ERR_OK;
}

/**
 * lwIP POST callback: Finish POST request
 *
 * Called when POST request completes
 */
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    upload_connection_state_t *state = (upload_connection_state_t *)*(void **)connection;

    if (state == NULL) {
        printf("ERROR: No upload state in finish callback\n");
        return;
    }

    printf("POST finished\n");

    // Generate response
    if (state->upload_error) {
        // Error response
        snprintf(response_uri, response_uri_len,
                 "/error.html?msg=%s", state->error_message);
        printf("Upload failed: %s\n", state->error_message);
    } else if (state->upload_started) {
        // Finalize firmware update
        if (state->expected_crc32 != 0) {
            if (firmware_manager_finalize_update(state->expected_crc32)) {
                snprintf(response_uri, response_uri_len,
                         "/success.html?msg=Firmware upload complete");
                printf("Upload successful!\n");
                state->upload_complete = true;
            } else {
                snprintf(response_uri, response_uri_len,
                         "/error.html?msg=Firmware validation failed");
                printf("Upload failed: validation error\n");
                state->upload_error = true;
            }
        } else {
            snprintf(response_uri, response_uri_len,
                     "/error.html?msg=Missing CRC32 for validation");
            printf("Upload failed: missing CRC32\n");
            state->upload_error = true;
            firmware_manager_cancel_update();
        }
    } else {
        snprintf(response_uri, response_uri_len,
                 "/error.html?msg=Upload not started");
    }

    // Clean up state
    mem_free(state);
    *(void **)connection = NULL;
}

bool firmware_upload_init(void) {
    printf("Firmware upload handler initialized\n");
    printf("Upload endpoint: POST %s\n", FIRMWARE_UPLOAD_URI);
    printf("Required headers:\n");
    printf("  - X-Firmware-Size: <size in bytes>\n");
    printf("  - X-Firmware-CRC32: <hex CRC32>\n");
    printf("  - X-Firmware-Version: <version string> (optional)\n");

    // Note: lwIP httpd automatically calls these callbacks for POST requests
    // No explicit registration needed - callbacks are defined globally

    return true;
}

const char *firmware_upload_get_uri(void) {
    return FIRMWARE_UPLOAD_URI;
}
