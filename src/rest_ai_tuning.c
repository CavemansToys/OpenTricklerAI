#include "rest_ai_tuning.h"
#include "ai_tuning.h"
#include "profile.h"
#include "http_rest.h"
#include "common.h"
#include "input_validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// JSON response buffer
static char ai_tuning_json_buffer[2048];

bool http_rest_ai_tuning_start(struct fs_file *file, int num_params,
                                 char *params[], char *values[]) {
    int profile_idx = -1;

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        if (strcmp(params[idx], "profile_idx") == 0) {
            profile_idx = atoi(values[idx]);
        }
    }

    if (profile_idx < 0 || profile_idx > 7) {
        int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
            "%s{\"success\":false,\"error\":\"Invalid profile_idx (must be 0-7)\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(ai_tuning_json_buffer)) {
            file->data = ai_tuning_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Get profile
    profile_t* profile = profile_select(profile_idx);
    if (!profile) {
        int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
            "%s{\"success\":false,\"error\":\"Failed to select profile\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(ai_tuning_json_buffer)) {
            file->data = ai_tuning_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Check if AI tuning enabled for this profile
    if (!profile->ai_tuning_enabled) {
        int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
            "%s{\"success\":false,\"error\":\"AI tuning not enabled for this profile\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(ai_tuning_json_buffer)) {
            file->data = ai_tuning_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Start tuning
    if (!ai_tuning_start(profile)) {
        int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
            "%s{\"success\":false,\"error\":\"Failed to start AI tuning\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(ai_tuning_json_buffer)) {
            file->data = ai_tuning_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Success
    int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
        "%s{\"success\":true,\"message\":\"AI tuning started\",\"profile\":\"%s\"}",
        http_json_header, profile->name);

    if (len < 0 || len >= (int)sizeof(ai_tuning_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    file->data = ai_tuning_json_buffer;
    file->len = len;
    file->index = len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool http_rest_ai_tuning_status(struct fs_file *file, int num_params,
                                  char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;

    ai_tuning_session_t* session = ai_tuning_get_session();

    const char* state_str;
    switch (session->state) {
        case AI_TUNING_IDLE:           state_str = "idle"; break;
        case AI_TUNING_PHASE_1_COARSE: state_str = "phase1_coarse"; break;
        case AI_TUNING_PHASE_2_FINE:   state_str = "phase2_fine"; break;
        case AI_TUNING_COMPLETE:       state_str = "complete"; break;
        case AI_TUNING_ERROR:          state_str = "error"; break;
        default:                       state_str = "unknown"; break;
    }

    bool is_active = ai_tuning_is_active();
    bool is_complete = ai_tuning_is_complete();
    uint8_t progress = ai_tuning_get_progress_percent();

    // Build JSON response
    int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
        "%s{"
        "\"state\":\"%s\","
        "\"is_active\":%s,"
        "\"is_complete\":%s,"
        "\"drops_completed\":%u,"
        "\"drops_target\":%u,"
        "\"drops_max\":%u,"
        "\"progress_percent\":%u",
        http_json_header,
        state_str,
        is_active ? "true" : "false",
        is_complete ? "true" : "false",
        session->drops_completed,
        session->total_drops_target,
        session->max_drops_allowed,
        progress);

    if (len < 0 || len >= (int)sizeof(ai_tuning_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    // Add current parameters if active
    if (is_active) {
        float coarse_kp, coarse_kd, fine_kp, fine_kd;
        if (ai_tuning_get_next_params(&coarse_kp, &coarse_kd, &fine_kp, &fine_kd)) {
            len += snprintf(ai_tuning_json_buffer + len, sizeof(ai_tuning_json_buffer) - len,
                ",\"current_params\":{"
                "\"coarse_kp\":%.4f,"
                "\"coarse_kd\":%.4f,"
                "\"fine_kp\":%.4f,"
                "\"fine_kd\":%.4f"
                "}",
                coarse_kp, coarse_kd, fine_kp, fine_kd);
        }
    }

    // Add recommended parameters if complete
    if (is_complete) {
        float coarse_kp, coarse_kd, fine_kp, fine_kd;
        if (ai_tuning_get_recommended_params(&coarse_kp, &coarse_kd, &fine_kp, &fine_kd)) {
            len += snprintf(ai_tuning_json_buffer + len, sizeof(ai_tuning_json_buffer) - len,
                ",\"recommended_params\":{"
                "\"coarse_kp\":%.4f,"
                "\"coarse_kd\":%.4f,"
                "\"fine_kp\":%.4f,"
                "\"fine_kd\":%.4f"
                "}",
                coarse_kp, coarse_kd, fine_kp, fine_kd);

            // Add statistics
            len += snprintf(ai_tuning_json_buffer + len, sizeof(ai_tuning_json_buffer) - len,
                ",\"statistics\":{"
                "\"avg_overthrow\":%.2f,"
                "\"avg_time\":%.1f,"
                "\"consistency_score\":%.1f"
                "}",
                session->avg_overthrow,
                session->avg_total_time,
                session->consistency_score);
        }
    }

    // Close JSON
    len += snprintf(ai_tuning_json_buffer + len, sizeof(ai_tuning_json_buffer) - len, "}");

    if (len < 0 || len >= (int)sizeof(ai_tuning_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    file->data = ai_tuning_json_buffer;
    file->len = len;
    file->index = len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool http_rest_ai_tuning_apply(struct fs_file *file, int num_params,
                                 char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;

    if (!ai_tuning_is_complete()) {
        int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
            "%s{\"success\":false,\"error\":\"AI tuning not complete\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(ai_tuning_json_buffer)) {
            file->data = ai_tuning_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    if (!ai_tuning_apply_params()) {
        int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
            "%s{\"success\":false,\"error\":\"Failed to apply parameters\"}",
            http_json_header);

        if (len >= 0 && len < (int)sizeof(ai_tuning_json_buffer)) {
            file->data = ai_tuning_json_buffer;
            file->len = len;
            file->index = len;
            file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        }
        return true;
    }

    // Save profile with new parameters
    profile_data_save();

    int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
        "%s{\"success\":true,\"message\":\"Parameters applied and saved\"}",
        http_json_header);

    if (len < 0 || len >= (int)sizeof(ai_tuning_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    file->data = ai_tuning_json_buffer;
    file->len = len;
    file->index = len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool http_rest_ai_tuning_cancel(struct fs_file *file, int num_params,
                                  char *params[], char *values[]) {
    (void)num_params;
    (void)params;
    (void)values;

    ai_tuning_cancel();

    int len = snprintf(ai_tuning_json_buffer, sizeof(ai_tuning_json_buffer),
        "%s{\"success\":true,\"message\":\"AI tuning cancelled\"}",
        http_json_header);

    if (len < 0 || len >= (int)sizeof(ai_tuning_json_buffer)) {
        return send_buffer_overflow_error(file);
    }

    file->data = ai_tuning_json_buffer;
    file->len = len;
    file->index = len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}

bool rest_ai_tuning_init(void) {
    // Register REST endpoints
    rest_register_handler("/rest/ai_tuning_start", http_rest_ai_tuning_start);
    rest_register_handler("/rest/ai_tuning_status", http_rest_ai_tuning_status);
    rest_register_handler("/rest/ai_tuning_apply", http_rest_ai_tuning_apply);
    rest_register_handler("/rest/ai_tuning_cancel", http_rest_ai_tuning_cancel);

    printf("AI Tuning REST endpoints registered:\n");
    printf("  - POST /rest/ai_tuning_start?profile_idx=X\n");
    printf("  - GET  /rest/ai_tuning_status\n");
    printf("  - POST /rest/ai_tuning_apply\n");
    printf("  - POST /rest/ai_tuning_cancel\n");

    return true;
}
