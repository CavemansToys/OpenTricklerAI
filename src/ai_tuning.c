#include "ai_tuning.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Global tuning session state
static ai_tuning_session_t g_session;
static ai_tuning_config_t g_config;
static bool g_initialized = false;

// Forward declarations
static void calculate_next_params_phase1(void);
static void calculate_next_params_phase2(void);
static float calculate_drop_score(const ai_drop_telemetry_t* drop);
static void finalize_recommendations(void);
static bool check_phase_convergence(uint8_t phase_start_idx, uint8_t phase_drop_count);

void ai_tuning_init(void) {
    if (g_initialized) {
        return;
    }

    // Initialize default configuration
    g_config.max_overthrow_percent = 6.67f;  // 1/15 overthrow
    g_config.target_coarse_time_ms = 10000.0f;
    g_config.target_total_time_ms = 15000.0f;

    // Cost function weights
    g_config.weight_overthrow = 10.0f;
    g_config.weight_time = 1.0f;
    g_config.weight_consistency = 5.0f;

    // Parameter search ranges (match app's built-in PID validation: 0.0-100.0)
    g_config.coarse_kp_min = 0.0f;
    g_config.coarse_kp_max = 100.0f;
    g_config.coarse_kd_min = 0.0f;
    g_config.coarse_kd_max = 100.0f;

    g_config.fine_kp_min = 0.0f;
    g_config.fine_kp_max = 100.0f;
    g_config.fine_kd_min = 0.0f;
    g_config.fine_kd_max = 100.0f;

    // Learning rates (use 0.1 increments as recommended by developer)
    g_config.learning_rate_kp = 0.1f;
    g_config.learning_rate_kd = 0.1f;

    // Clear session
    memset(&g_session, 0, sizeof(ai_tuning_session_t));
    g_session.state = AI_TUNING_IDLE;

    g_initialized = true;

    printf("AI Tuning System initialized\n");
}

ai_tuning_config_t* ai_tuning_get_config(void) {
    return &g_config;
}

bool ai_tuning_start(profile_t* profile) {
    if (!g_initialized) {
        ai_tuning_init();
    }

    if (profile == NULL) {
        printf("AI Tuning: Invalid profile\n");
        return false;
    }

    // Initialize session
    memset(&g_session, 0, sizeof(ai_tuning_session_t));
    g_session.state = AI_TUNING_PHASE_1_COARSE;
    g_session.target_profile = profile;
    g_session.drops_completed = 0;
    g_session.total_drops_target = 6;     // Target 6 drops (3 per phase)
    g_session.max_drops_allowed = 10;     // Max 10 drops total (practical limit)
    g_session.min_drops_per_phase = 2;    // Minimum 2 drops before checking convergence

    // Initialize coarse parameter search space
    g_session.coarse_kp_min = g_config.coarse_kp_min;
    g_session.coarse_kp_max = g_config.coarse_kp_max;
    g_session.coarse_kd_min = g_config.coarse_kd_min;
    g_session.coarse_kd_max = g_config.coarse_kd_max;

    // Start at 0.1 as recommended by developer (tuning baseline)
    // Use profile values if they exist and are reasonable, otherwise start at 0.1
    if (profile->coarse_kp >= 0.1f && profile->coarse_kp <= 100.0f) {
        g_session.coarse_kp_best = profile->coarse_kp;
    } else {
        g_session.coarse_kp_best = 0.1f;  // Developer-recommended starting point
    }
    if (profile->coarse_kd >= 0.1f && profile->coarse_kd <= 100.0f) {
        g_session.coarse_kd_best = profile->coarse_kd;
    } else {
        g_session.coarse_kd_best = 0.1f;  // Developer-recommended starting point
    }
    g_session.coarse_best_score = -1.0f;  // Not yet evaluated

    // Initialize fine parameter search space
    g_session.fine_kp_min = g_config.fine_kp_min;
    g_session.fine_kp_max = g_config.fine_kp_max;
    g_session.fine_kd_min = g_config.fine_kd_min;
    g_session.fine_kd_max = g_config.fine_kd_max;

    // Start at 0.1 for fine trickler too
    if (profile->fine_kp >= 0.1f && profile->fine_kp <= 100.0f) {
        g_session.fine_kp_best = profile->fine_kp;
    } else {
        g_session.fine_kp_best = 0.1f;  // Developer-recommended starting point
    }
    if (profile->fine_kd >= 0.1f && profile->fine_kd <= 100.0f) {
        g_session.fine_kd_best = profile->fine_kd;
    } else {
        g_session.fine_kd_best = 0.1f;  // Developer-recommended starting point
    }
    g_session.fine_best_score = -1.0f;  // Not yet evaluated

    printf("\n================================================\n");
    printf("AI PID Auto-Tuning Started\n");
    printf("================================================\n");
    printf("Profile: %s\n", profile->name);
    printf("Target drops: %d (adaptive, may need more)\n", g_session.total_drops_target);
    printf("Max drops: %d (safety limit)\n", g_session.max_drops_allowed);
    printf("Phase 1: Tune coarse trickler (until converged)\n");
    printf("Phase 2: Tune fine trickler (until converged)\n");
    printf("Algorithm will adapt iterations as needed!\n");
    printf("================================================\n\n");

    return true;
}

bool ai_tuning_get_next_params(float* coarse_kp, float* coarse_kd,
                                 float* fine_kp, float* fine_kd) {
    if (!ai_tuning_is_active()) {
        return false;
    }

    if (g_session.state == AI_TUNING_PHASE_1_COARSE) {
        // Phase 1: Return current coarse parameters, use profile fine params
        *coarse_kp = g_session.coarse_kp_best;
        *coarse_kd = g_session.coarse_kd_best;
        *fine_kp = g_session.target_profile->fine_kp;
        *fine_kd = g_session.target_profile->fine_kd;
    }
    else if (g_session.state == AI_TUNING_PHASE_2_FINE) {
        // Phase 2: Use optimized coarse params, tune fine params
        *coarse_kp = g_session.recommended_coarse_kp;
        *coarse_kd = g_session.recommended_coarse_kd;
        *fine_kp = g_session.fine_kp_best;
        *fine_kd = g_session.fine_kd_best;
    }
    else {
        return false;
    }

    return true;
}

bool ai_tuning_record_drop(const ai_drop_telemetry_t* telemetry) {
    if (!ai_tuning_is_active() || telemetry == NULL) {
        return false;
    }

    if (g_session.drops_completed >= g_session.max_drops_allowed) {
        printf("AI Tuning: Already reached maximum %d drops\n", g_session.max_drops_allowed);
        return false;
    }

    // Store telemetry
    uint8_t idx = g_session.drops_completed;
    memcpy(&g_session.drops[idx], telemetry, sizeof(ai_drop_telemetry_t));

    // Calculate score for this drop
    float drop_score = calculate_drop_score(telemetry);
    g_session.drops[idx].overall_score = drop_score;

    g_session.drops_completed++;

    printf("\n------------------------------------------------\n");
    printf("Drop %d/%d completed\n", g_session.drops_completed, g_session.total_drops_target);
    printf("------------------------------------------------\n");
    printf("Coarse Kp: %.4f, Kd: %.4f\n", telemetry->coarse_kp_used, telemetry->coarse_kd_used);
    printf("Fine Kp: %.4f, Kd: %.4f\n", telemetry->fine_kp_used, telemetry->fine_kd_used);
    printf("Overthrow: %.3f (%.2f%%)\n", telemetry->overthrow, telemetry->overthrow_percent);
    printf("Time: Coarse=%.1fms, Fine=%.1fms, Total=%.1fms\n",
           telemetry->coarse_time_ms, telemetry->fine_time_ms, telemetry->total_time_ms);
    printf("Score: %.2f\n", drop_score);
    printf("------------------------------------------------\n\n");

    // Update algorithm state based on phase
    if (g_session.state == AI_TUNING_PHASE_1_COARSE) {
        // Update best coarse parameters if this is better
        if (g_session.coarse_best_score < 0 || drop_score > g_session.coarse_best_score) {
            g_session.coarse_best_score = drop_score;
            g_session.coarse_kp_best = telemetry->coarse_kp_used;
            g_session.coarse_kd_best = telemetry->coarse_kd_used;
        }

        // Calculate next parameters for next drop
        calculate_next_params_phase1();

        // Check if phase 1 should complete
        uint8_t phase_drops = g_session.drops_completed;
        bool should_complete_phase1 = false;

        // Check convergence if we've done minimum drops
        if (phase_drops >= g_session.min_drops_per_phase) {
            if (check_phase_convergence(0, phase_drops)) {
                should_complete_phase1 = true;
            }
        }

        // Force completion if we hit max drops for this phase (5 drops max per phase)
        if (phase_drops >= 5) {
            printf("AI Tuning: Phase 1 reached max drops (5), moving to phase 2\n");
            should_complete_phase1 = true;
        }

        if (should_complete_phase1) {
            printf("\n================================================\n");
            printf("Phase 1 Complete - Coarse Trickler Tuned\n");
            printf("================================================\n");
            printf("Drops in phase: %d\n", phase_drops);
            printf("Best Coarse Kp: %.4f\n", g_session.coarse_kp_best);
            printf("Best Coarse Kd: %.4f\n", g_session.coarse_kd_best);
            printf("Best Score: %.2f\n", g_session.coarse_best_score);
            printf("\nStarting Phase 2: Fine Trickler Tuning...\n");
            printf("================================================\n\n");

            // Store recommended coarse params
            g_session.recommended_coarse_kp = g_session.coarse_kp_best;
            g_session.recommended_coarse_kd = g_session.coarse_kd_best;

            // Move to phase 2
            g_session.state = AI_TUNING_PHASE_2_FINE;
        }
    }
    else if (g_session.state == AI_TUNING_PHASE_2_FINE) {
        // Update best fine parameters if this is better
        if (g_session.fine_best_score < 0 || drop_score > g_session.fine_best_score) {
            g_session.fine_best_score = drop_score;
            g_session.fine_kp_best = telemetry->fine_kp_used;
            g_session.fine_kd_best = telemetry->fine_kd_used;
        }

        // Calculate next parameters for next drop
        calculate_next_params_phase2();

        // Check if phase 2 should complete
        // Find where phase 2 started (after phase 1 completed)
        uint8_t phase2_start_idx = 0;
        for (uint8_t i = 0; i < g_session.drops_completed; i++) {
            if (g_session.drops[i].fine_kp_used != g_session.recommended_coarse_kp) {
                phase2_start_idx = i;
                break;
            }
        }
        uint8_t phase2_drops = g_session.drops_completed - phase2_start_idx;
        bool should_complete_phase2 = false;

        // Check convergence if we've done minimum drops
        if (phase2_drops >= g_session.min_drops_per_phase) {
            if (check_phase_convergence(phase2_start_idx, phase2_drops)) {
                should_complete_phase2 = true;
            }
        }

        // Force completion if we hit max drops for this phase or overall max
        if (phase2_drops >= 5 || g_session.drops_completed >= g_session.max_drops_allowed) {
            printf("AI Tuning: Phase 2 reached max drops, completing tuning\n");
            should_complete_phase2 = true;
        }

        if (should_complete_phase2) {
            finalize_recommendations();
        }
    }

    return true;
}

static bool check_phase_convergence(uint8_t phase_start_idx, uint8_t phase_drop_count) {
    // Check if this phase has converged (stable performance)
    // Need at least 2 drops to check convergence
    if (phase_drop_count < 2) {
        return false;
    }

    // Check last 2 drops for quick convergence
    uint8_t idx1 = phase_start_idx + phase_drop_count - 2;
    uint8_t idx2 = phase_start_idx + phase_drop_count - 1;

    ai_drop_telemetry_t* drop1 = &g_session.drops[idx1];
    ai_drop_telemetry_t* drop2 = &g_session.drops[idx2];

    // Check if performance is improving or stable
    float score_change = drop2->overall_score - drop1->overall_score;
    float avg_overthrow = (fabsf(drop1->overthrow_percent) + fabsf(drop2->overthrow_percent)) / 2.0f;

    // Converged if:
    // 1. Overthrow acceptable in both drops AND
    // 2. Score change is small (improving less than 2% OR getting slightly worse)
    bool overthrow_good = (fabsf(drop1->overthrow_percent) < g_config.max_overthrow_percent &&
                           fabsf(drop2->overthrow_percent) < g_config.max_overthrow_percent);
    bool score_stable = fabsf(score_change) < 2.0f;  // Less than 2 point change

    if (overthrow_good && score_stable) {
        printf("AI Tuning: Phase converged after %d drops\n", phase_drop_count);
        printf("  Avg overthrow: %.2f%%, Score stable\n", avg_overthrow);
        return true;
    }

    return false;
}

static float calculate_drop_score(const ai_drop_telemetry_t* drop) {
    // Calculate individual component scores (0-100)

    // 1. Overthrow score (100 = no overthrow, 0 = max overthrow)
    float overthrow_magnitude = fabsf(drop->overthrow_percent);
    float overthrow_score = 100.0f * fmaxf(0.0f, 1.0f - (overthrow_magnitude / g_config.max_overthrow_percent));

    // 2. Speed score (100 = instant, decreases with time)
    float time_ratio = drop->total_time_ms / g_config.target_total_time_ms;
    float speed_score = 100.0f * fmaxf(0.0f, 2.0f - time_ratio);  // Allow faster than target

    // 3. Accuracy score (based on final error)
    float error = fabsf(drop->final_weight - drop->target_weight);
    float error_percent = 100.0f * error / drop->target_weight;
    float accuracy_score = 100.0f * fmaxf(0.0f, 1.0f - error_percent);

    // Weighted combination
    float total_score = (g_config.weight_overthrow * overthrow_score +
                         g_config.weight_time * speed_score +
                         accuracy_score) / (g_config.weight_overthrow + g_config.weight_time + 1.0f);

    return total_score;
}

static void calculate_next_params_phase1(void) {
    // Adaptive parameter adjustment for coarse trickler
    // Using 0.1 increments as recommended by developer
    uint8_t drops_in_phase = g_session.drops_completed;
    ai_drop_telemetry_t* prev_drop = drops_in_phase > 0 ? &g_session.drops[drops_in_phase - 1] : NULL;

    if (drops_in_phase == 1) {
        // Drop 2: Test with Kp increased by 0.1
        g_session.coarse_kp_best += 0.1f;

        // Adaptive range expansion: if we hit max and need more, expand
        if (g_session.coarse_kp_best >= g_session.coarse_kp_max) {
            g_session.coarse_kp_max += 1.0f;  // Expand range by 1.0
            printf("AI Tuning: Expanding coarse Kp range to %.1f\n", g_session.coarse_kp_max);
        }
    }
    else if (drops_in_phase == 2) {
        // Drop 3: Adjust based on overthrow from drop 2
        if (prev_drop->overthrow_percent > g_config.max_overthrow_percent) {
            // Too much overthrow, reduce Kp by 0.1, increase Kd by 0.1
            g_session.coarse_kp_best -= 0.1f;
            g_session.coarse_kd_best += 0.1f;
        } else if (prev_drop->overthrow_percent < 1.0f) {
            // Very little overthrow, can increase Kp for speed
            g_session.coarse_kp_best += 0.1f;
        } else {
            // Good balance, tune Kd for precision
            g_session.coarse_kd_best += 0.1f;
        }
    }
    else {
        // Drops 4-5: Refine based on performance trend
        ai_drop_telemetry_t* curr = &g_session.drops[drops_in_phase - 1];
        ai_drop_telemetry_t* prev = &g_session.drops[drops_in_phase - 2];

        // If getting worse, reverse direction
        if (curr->overall_score < prev->overall_score) {
            // Reverse: reduce Kp, increase Kd
            g_session.coarse_kp_best -= 0.1f;
            g_session.coarse_kd_best += 0.1f;
        } else {
            // Getting better, continue tuning
            if (curr->overthrow_percent > g_config.max_overthrow_percent) {
                g_session.coarse_kd_best += 0.1f;  // More damping
            } else if (curr->total_time_ms > g_config.target_coarse_time_ms) {
                g_session.coarse_kp_best += 0.1f;  // More speed
            }
        }
    }

    // Clamp to valid ranges
    g_session.coarse_kp_best = fmaxf(g_session.coarse_kp_min, fminf(g_session.coarse_kp_best, g_session.coarse_kp_max));
    g_session.coarse_kd_best = fmaxf(g_session.coarse_kd_min, fminf(g_session.coarse_kd_best, g_session.coarse_kd_max));
}

static void calculate_next_params_phase2(void) {
    // Adaptive parameter adjustment for fine trickler
    // Using 0.1 increments as recommended by developer
    uint8_t drops_in_phase = g_session.drops_completed - 5;  // Phase 2 starts at drop 6
    ai_drop_telemetry_t* prev_drop = g_session.drops_completed > 0 ?
        &g_session.drops[g_session.drops_completed - 1] : NULL;

    if (drops_in_phase == 1) {
        // Drop 7: Test with Kp increased by 0.1
        g_session.fine_kp_best += 0.1f;

        // Adaptive range expansion: if we hit max and need more, expand
        if (g_session.fine_kp_best >= g_session.fine_kp_max) {
            g_session.fine_kp_max += 1.0f;  // Expand range by 1.0
            printf("AI Tuning: Expanding fine Kp range to %.1f\n", g_session.fine_kp_max);
        }
    }
    else if (drops_in_phase == 2) {
        // Drop 8: Adjust based on overthrow from drop 7
        if (prev_drop->overthrow_percent > g_config.max_overthrow_percent) {
            // Too much overthrow, reduce Kp by 0.1, increase Kd by 0.1
            g_session.fine_kp_best -= 0.1f;
            g_session.fine_kd_best += 0.1f;
        } else if (prev_drop->overthrow_percent < 0.5f) {
            // Very precise, can increase Kp for speed
            g_session.fine_kp_best += 0.1f;
        } else {
            // Good balance, tune Kd for more precision
            g_session.fine_kd_best += 0.1f;
        }
    }
    else {
        // Drops 9-10: Refine based on performance trend
        ai_drop_telemetry_t* curr = &g_session.drops[g_session.drops_completed - 1];
        ai_drop_telemetry_t* prev = &g_session.drops[g_session.drops_completed - 2];

        // If getting worse, reverse direction
        if (curr->overall_score < prev->overall_score) {
            // Reverse: reduce Kp, increase Kd
            g_session.fine_kp_best -= 0.1f;
            g_session.fine_kd_best += 0.1f;
        } else {
            // Getting better, continue tuning
            if (curr->overthrow_percent > g_config.max_overthrow_percent) {
                g_session.fine_kd_best += 0.1f;  // More damping
            } else if (curr->fine_time_ms > (g_config.target_total_time_ms - g_config.target_coarse_time_ms)) {
                g_session.fine_kp_best += 0.1f;  // More speed
            }
        }

        // Adaptive range expansion if needed
        if (g_session.fine_kp_best >= g_session.fine_kp_max &&
            curr->overthrow_percent < g_config.max_overthrow_percent) {
            g_session.fine_kp_max += 1.0f;
            printf("AI Tuning: Expanding fine Kp range to %.1f\n", g_session.fine_kp_max);
        }
        if (g_session.fine_kd_best >= g_session.fine_kd_max &&
            curr->overthrow_percent > 1.0f) {
            g_session.fine_kd_max += 1.0f;
            printf("AI Tuning: Expanding fine Kd range to %.1f\n", g_session.fine_kd_max);
        }
    }

    // Clamp to valid ranges
    g_session.fine_kp_best = fmaxf(g_session.fine_kp_min, fminf(g_session.fine_kp_best, g_session.fine_kp_max));
    g_session.fine_kd_best = fmaxf(g_session.fine_kd_min, fminf(g_session.fine_kd_best, g_session.fine_kd_max));
}

static void finalize_recommendations(void) {
    // Calculate statistics across all drops
    float total_overthrow = 0.0f;
    float total_time = 0.0f;
    float max_overthrow = 0.0f;
    float min_overthrow = 999.0f;

    for (uint8_t i = 0; i < g_session.total_drops_target; i++) {
        total_overthrow += fabsf(g_session.drops[i].overthrow_percent);
        total_time += g_session.drops[i].total_time_ms;

        float overthrow_abs = fabsf(g_session.drops[i].overthrow_percent);
        if (overthrow_abs > max_overthrow) max_overthrow = overthrow_abs;
        if (overthrow_abs < min_overthrow) min_overthrow = overthrow_abs;
    }

    g_session.avg_overthrow = total_overthrow / g_session.total_drops_target;
    g_session.avg_total_time = total_time / g_session.total_drops_target;

    // Calculate consistency (variance in overthrow)
    float variance = (max_overthrow - min_overthrow) / fmaxf(g_session.avg_overthrow, 0.01f);
    g_session.consistency_score = 100.0f * fmaxf(0.0f, 1.0f - variance);

    // Store final recommendations (already set from best scores)
    g_session.recommended_fine_kp = g_session.fine_kp_best;
    g_session.recommended_fine_kd = g_session.fine_kd_best;

    g_session.state = AI_TUNING_COMPLETE;

    printf("\n================================================\n");
    printf("AI PID Auto-Tuning COMPLETE!\n");
    printf("================================================\n");
    printf("RECOMMENDED PARAMETERS:\n");
    printf("  Coarse Kp: %.4f  Kd: %.4f\n",
           g_session.recommended_coarse_kp, g_session.recommended_coarse_kd);
    printf("  Fine   Kp: %.4f  Kd: %.4f\n",
           g_session.recommended_fine_kp, g_session.recommended_fine_kd);
    printf("\nPERFORMANCE STATISTICS:\n");
    printf("  Average Overthrow: %.2f%%\n", g_session.avg_overthrow);
    printf("  Average Time: %.1f ms\n", g_session.avg_total_time);
    printf("  Consistency Score: %.1f/100\n", g_session.consistency_score);
    printf("\nPlease review and confirm to apply these parameters.\n");
    printf("================================================\n\n");
}

bool ai_tuning_is_complete(void) {
    return g_session.state == AI_TUNING_COMPLETE;
}

ai_tuning_session_t* ai_tuning_get_session(void) {
    return &g_session;
}

bool ai_tuning_get_recommended_params(float* coarse_kp, float* coarse_kd,
                                       float* fine_kp, float* fine_kd) {
    if (g_session.state != AI_TUNING_COMPLETE) {
        return false;
    }

    *coarse_kp = g_session.recommended_coarse_kp;
    *coarse_kd = g_session.recommended_coarse_kd;
    *fine_kp = g_session.recommended_fine_kp;
    *fine_kd = g_session.recommended_fine_kd;

    return true;
}

bool ai_tuning_apply_params(void) {
    if (g_session.state != AI_TUNING_COMPLETE || g_session.target_profile == NULL) {
        return false;
    }

    // Apply recommended parameters to profile
    g_session.target_profile->coarse_kp = g_session.recommended_coarse_kp;
    g_session.target_profile->coarse_kd = g_session.recommended_coarse_kd;
    g_session.target_profile->fine_kp = g_session.recommended_fine_kp;
    g_session.target_profile->fine_kd = g_session.recommended_fine_kd;

    printf("AI Tuning: Parameters applied to profile '%s'\n", g_session.target_profile->name);
    printf("  Coarse Kp: %.4f  Kd: %.4f\n",
           g_session.target_profile->coarse_kp, g_session.target_profile->coarse_kd);
    printf("  Fine   Kp: %.4f  Kd: %.4f\n",
           g_session.target_profile->fine_kp, g_session.target_profile->fine_kd);

    // Reset session
    g_session.state = AI_TUNING_IDLE;

    return true;
}

void ai_tuning_cancel(void) {
    printf("AI Tuning: Session cancelled\n");
    g_session.state = AI_TUNING_IDLE;
    memset(&g_session, 0, sizeof(ai_tuning_session_t));
}

bool ai_tuning_is_active(void) {
    return g_session.state == AI_TUNING_PHASE_1_COARSE ||
           g_session.state == AI_TUNING_PHASE_2_FINE;
}

uint8_t ai_tuning_get_progress_percent(void) {
    if (g_session.total_drops_target == 0) {
        return 0;
    }
    return (100 * g_session.drops_completed) / g_session.total_drops_target;
}

float ai_tuning_calculate_cost(float overthrow, float time_ms, float variance) {
    float cost = g_config.weight_overthrow * fabsf(overthrow) +
                 g_config.weight_time * (time_ms / g_config.target_total_time_ms) +
                 g_config.weight_consistency * variance;
    return cost;
}
