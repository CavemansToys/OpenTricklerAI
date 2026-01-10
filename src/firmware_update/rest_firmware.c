#include "rest_firmware.h"
#include "firmware_manager.h"
#include "firmware_download.h"
#include "../http_rest.h"
#include "../common.h"
#include "../input_validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * REST API Firmware Endpoints Implementation
 */

// JSON response buffer
static char firmware_json_buffer[1024];

bool http_rest_firmware_status(struct fs_file *file, int num_params,
                                char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;

    // Get current bank
    firmware_bank_t current_bank = firmware_manager_get_current_bank();
    const char *current_bank_str = (current_bank == BANK_A) ? "A" :
                                    (current_bank == BANK_B) ? "B" : "UNKNOWN";

    // Get bank A info
    firmware_info_t bank_a_info;
    bool bank_a_ok = firmware_manager_get_bank_info(BANK_A, &bank_a_info);

    // Get bank B info
    firmware_info_t bank_b_info;
    bool bank_b_ok = firmware_manager_get_bank_info(BANK_B, &bank_b_info);

    // Get update status
    firmware_update_status_t update_status;
    firmware_manager_get_status(&update_status);

    const char *state_str;
    switch (update_status.state) {
        case FIRMWARE_UPDATE_IDLE:       state_str = "idle"; break;
        case FIRMWARE_UPDATE_PREPARING:  state_str = "preparing"; break;
        case FIRMWARE_UPDATE_ERASING:    state_str = "erasing"; break;
        case FIRMWARE_UPDATE_RECEIVING:  state_str = "receiving"; break;
        case FIRMWARE_UPDATE_VALIDATING: state_str = "validating"; break;
        case FIRMWARE_UPDATE_COMPLETE:   state_str = "complete"; break;
        case FIRMWARE_UPDATE_ERROR:      state_str = "error"; break;
        default:                         state_str = "unknown"; break;
    }

    const char *target_bank_str = (update_status.target_bank == BANK_A) ? "A" :
                                   (update_status.target_bank == BANK_B) ? "B" : "none";

    // Check if rollback occurred
    bool rollback_occurred = firmware_manager_did_rollback_occur();

    // Build JSON response
    int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
        "%s"
        "{"
        "\"current_bank\":\"%s\","
        "\"bank_a\":{"
            "\"valid\":%s,"
            "\"size\":%lu,"
            "\"crc32\":\"0x%08lx\","
            "\"version\":\"%s\","
            "\"boot_count\":%u"
        "},"
        "\"bank_b\":{"
            "\"valid\":%s,"
            "\"size\":%lu,"
            "\"crc32\":\"0x%08lx\","
            "\"version\":\"%s\","
            "\"boot_count\":%u"
        "},"
        "\"update_status\":{"
            "\"state\":\"%s\","
            "\"progress\":%lu,"
            "\"target_bank\":\"%s\","
            "\"bytes_received\":%lu,"
            "\"total_bytes\":%lu,"
            "\"error\":\"%s\""
        "},"
        "\"rollback_occurred\":%s"
        "}",
        http_json_header,
        current_bank_str,
        bank_a_ok ? (bank_a_info.valid ? "true" : "false") : "false",
        bank_a_ok ? bank_a_info.size : 0,
        bank_a_ok ? bank_a_info.crc32 : 0,
        bank_a_ok ? bank_a_info.version : "",
        bank_a_ok ? bank_a_info.boot_count : 0,
        bank_b_ok ? (bank_b_info.valid ? "true" : "false") : "false",
        bank_b_ok ? bank_b_info.size : 0,
        bank_b_ok ? bank_b_info.crc32 : 0,
        bank_b_ok ? bank_b_info.version : "",
        bank_b_ok ? bank_b_info.boot_count : 0,
        state_str,
        update_status.progress_percent,
        target_bank_str,
        update_status.bytes_received,
        update_status.total_bytes,
        update_status.error_message,
        rollback_occurred ? "true" : "false"
    );

    if (len < 0 || len >= (int)sizeof(firmware_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    size_t data_length = strlen(firmware_json_buffer);
    file->data = firmware_json_buffer;
    file->len = data_length;
    file->index = data_length;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool http_rest_firmware_activate(struct fs_file *file, int num_params,
                                  char *params[], char *values[]) {
    (void)file;
    (void)num_params;
    (void)params;
    (void)values;

    printf("Firmware activation requested via REST API\n");

    // Check if update is complete
    firmware_update_status_t status;
    firmware_manager_get_status(&status);

    if (status.state != FIRMWARE_UPDATE_COMPLETE) {
        int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
            "%s{\"success\":false,\"error\":\"No completed update to activate\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(firmware_json_buffer)) {
            file->data = firmware_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Send success response (system will reboot immediately after)
    int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
        "%s{\"success\":true,\"message\":\"Activating new firmware, system rebooting...\"}",
        http_json_header);

    if (len >= 0 && len < (int)sizeof(firmware_json_buffer)) {
        file->data = firmware_json_buffer;
        file->len = len;
        file->index = len;
        file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    }

    // Activate and reboot (does not return)
    firmware_manager_activate_and_reboot();

    // Should never reach here
    return true;
}

bool http_rest_firmware_rollback(struct fs_file *file, int num_params,
                                  char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;

    printf("Firmware rollback requested via REST API\n");

    // Attempt rollback
    if (!firmware_manager_rollback_and_reboot()) {
        // Rollback failed (no valid backup)
        int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
            "%s{\"success\":false,\"error\":\"Rollback failed - no valid backup firmware\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(firmware_json_buffer)) {
            file->data = firmware_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Should never reach here (rollback reboots)
    return true;
}

bool http_rest_firmware_cancel(struct fs_file *file, int num_params,
                                char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;

    printf("Firmware update cancellation requested via REST API\n");

    // Cancel update
    firmware_manager_cancel_update();

    // Send success response
    int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
        "%s{\"success\":true,\"message\":\"Firmware update cancelled\"}",
        http_json_header);

    if (len < 0 || len >= (int)sizeof(firmware_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    size_t data_length = strlen(firmware_json_buffer);
    file->data = firmware_json_buffer;
    file->len = data_length;
    file->index = data_length;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool http_rest_firmware_download(struct fs_file *file, int num_params,
                                  char *params[], char *values[]) {
    char *url = NULL;
    uint32_t expected_crc32 = 0;
    char *expected_version = NULL;

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        if (strcmp(params[idx], "url") == 0) {
            url = values[idx];
        } else if (strcmp(params[idx], "crc32") == 0) {
            expected_crc32 = strtoul(values[idx], NULL, 16);
        } else if (strcmp(params[idx], "version") == 0) {
            expected_version = values[idx];
        }
    }

    if (url == NULL) {
        int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
            "%s{\"success\":false,\"error\":\"Missing 'url' parameter\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(firmware_json_buffer)) {
            file->data = firmware_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    printf("Firmware download requested: url=%s, crc32=0x%08lx\n", url, expected_crc32);

    // Start download
    if (!firmware_download_start(url, expected_crc32, expected_version)) {
        int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
            "%s{\"success\":false,\"error\":\"Failed to start download\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(firmware_json_buffer)) {
            file->data = firmware_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Success response
    int len = snprintf(firmware_json_buffer, sizeof(firmware_json_buffer),
        "%s{\"success\":true,\"message\":\"Firmware download started\",\"url\":\"%s\"}",
        http_json_header, url);

    if (len < 0 || len >= (int)sizeof(firmware_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    size_t data_length = strlen(firmware_json_buffer);
    file->data = firmware_json_buffer;
    file->len = data_length;
    file->index = data_length;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool rest_firmware_init(void) {
    // Register REST endpoints
    rest_register_handler("/rest/firmware_status", http_rest_firmware_status);
    rest_register_handler("/rest/firmware_download", http_rest_firmware_download);
    rest_register_handler("/rest/firmware_activate", http_rest_firmware_activate);
    rest_register_handler("/rest/firmware_rollback", http_rest_firmware_rollback);
    rest_register_handler("/rest/firmware_cancel", http_rest_firmware_cancel);

    printf("Firmware REST endpoints registered:\n");
    printf("  - GET  /rest/firmware_status\n");
    printf("  - GET  /rest/firmware_download?url=<url>&crc32=<hex>&version=<ver>\n");
    printf("  - POST /rest/firmware_activate\n");
    printf("  - POST /rest/firmware_rollback\n");
    printf("  - POST /rest/firmware_cancel\n");

    return true;
}
