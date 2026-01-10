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
    g_session.total_drops_target = 4;     // RP2350: Target 4 drops (2 per phase, adaptive)
    g_session.max_drops_allowed = 50;     // RP2350: Max 50 drops (leveraging 520KB RAM)
    g_session.min_drops_per_phase = 2;    // Minimum 2 drops before checking convergence
    g_session.exploration_factor = 0.5f;  // RP2350: Start with balanced exploration/exploitation
    g_session.consecutive_good_drops = 0; // RP2350: Track performance streak

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
    printf("AI PID Auto-Tuning Started (RP2350 Enhanced)\n");
    printf("================================================\n");
    printf("Profile: %s\n", profile->name);
    printf("Target drops: %d (smart convergence, adapts as needed)\n", g_session.total_drops_target);
    printf("Max drops: %d (extended history)\n", g_session.max_drops_allowed);
    printf("Phase 1: Tune coarse trickler (target 2-3 drops)\n");
    printf("Phase 2: Tune fine trickler (target 2-3 drops)\n");
    printf("RP2350 features: Double precision, Bayesian optimization\n");
    printf("Smart convergence: Fewer drops with better results!\n");
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

            // RP2350: Reset consecutive good drops counter for phase 2
            g_session.consecutive_good_drops = 0;
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
    // RP2350 Enhanced: Smart convergence detection for fewer drops
    // Need at least 2 drops to check convergence
    if (phase_drop_count < 2) {
        return false;
    }

    // Check last 2 drops for quick convergence
    uint8_t idx1 = phase_start_idx + phase_drop_count - 2;
    uint8_t idx2 = phase_start_idx + phase_drop_count - 1;

    ai_drop_telemetry_t* drop1 = &g_session.drops[idx1];
    ai_drop_telemetry_t* drop2 = &g_session.drops[idx2];

    // RP2350: Use double precision for accurate statistical comparison
    double avg_overthrow = ((double)fabsf(drop1->overthrow_percent) +
                            (double)fabsf(drop2->overthrow_percent)) / 2.0;
    float score_change = drop2->overall_score - drop1->overall_score;

    // Smart convergence criteria:
    // 1. EXCELLENT: Both drops under 3% overthrow AND score > 80 → converge immediately
    bool excellent = (fabsf(drop1->overthrow_percent) < 3.0f &&
                      fabsf(drop2->overthrow_percent) < 3.0f &&
                      drop1->overall_score > 80.0f && drop2->overall_score > 80.0f);

    // 2. GOOD: Both drops acceptable AND score stable/improving
    bool overthrow_acceptable = (fabsf(drop1->overthrow_percent) < g_config.max_overthrow_percent &&
                                  fabsf(drop2->overthrow_percent) < g_config.max_overthrow_percent);
    bool score_stable = (score_change >= -1.0f);  // Not getting worse by more than 1 point

    // 3. Track consecutive good drops for faster convergence
    if (overthrow_acceptable && drop2->overall_score > 75.0f) {
        g_session.consecutive_good_drops++;
    } else {
        g_session.consecutive_good_drops = 0;
    }

    // Converge if excellent OR (good + stable) OR (2+ consecutive good drops)
    if (excellent) {
        printf("AI Tuning: EXCELLENT performance! Phase converged after %d drops\n", phase_drop_count);
        printf("  Avg overthrow: %.2f%%, Score: %.1f (outstanding!)\n", avg_overthrow, drop2->overall_score);
        return true;
    }

    if (overthrow_acceptable && score_stable) {
        printf("AI Tuning: Phase converged after %d drops\n", phase_drop_count);
        printf("  Avg overthrow: %.2f%%, Score stable at %.1f\n", avg_overthrow, drop2->overall_score);
        return true;
    }

    if (g_session.consecutive_good_drops >= 2) {
        printf("AI Tuning: Consistent good performance! Phase converged after %d drops\n", phase_drop_count);
        printf("  %d consecutive good drops, avg overthrow: %.2f%%\n",
               g_session.consecutive_good_drops, avg_overthrow);
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
    // RP2350 Enhanced: Bayesian-inspired parameter selection
    // Balances exploration (trying new areas) vs exploitation (refining good areas)
    uint8_t drops_in_phase = g_session.drops_completed;
    ai_drop_telemetry_t* prev_drop = drops_in_phase > 0 ? &g_session.drops[drops_in_phase - 1] : NULL;

    // Adaptive step size: larger when exploring, smaller when exploiting
    float base_step = 0.1f;
    float step_kp = base_step * (1.0f + g_session.exploration_factor);
    float step_kd = base_step * (1.0f + g_session.exploration_factor * 0.5f);

    if (drops_in_phase == 1) {
        // Drop 2: Intelligent first exploration
        // Test direction based on current score
        if (prev_drop && prev_drop->overall_score < 70.0f) {
            // Poor performance, explore more aggressively
            g_session.coarse_kp_best += step_kp * 2.0f;  // Larger step
            g_session.exploration_factor = 0.8f;  // More exploration
        } else {
            // Decent start, moderate exploration
            g_session.coarse_kp_best += step_kp;
            g_session.exploration_factor = 0.5f;
        }
    }
    else if (drops_in_phase == 2) {
        // Drop 3: Smart adjustment based on performance
        ai_drop_telemetry_t* drop1 = &g_session.drops[0];
        ai_drop_telemetry_t* drop2 = &g_session.drops[1];

        // RP2350: Use double precision for gradient calculation
        double score_gradient = (double)drop2->overall_score - (double)drop1->overall_score;

        if (drop2->overall_score > 80.0f) {
            // Excellent result! Switch to exploitation (fine-tuning)
            g_session.exploration_factor = 0.2f;
            // Small refinement
            if (drop2->overthrow_percent > 2.0f) {
                g_session.coarse_kd_best += step_kd * 0.5f;
            } else {
                g_session.coarse_kp_best += step_kp * 0.5f;
            }
        } else if (score_gradient > 5.0) {
            // Improving fast! Continue in same direction
            g_session.exploration_factor = 0.3f;
            if (prev_drop->overthrow_percent < g_config.max_overthrow_percent) {
                g_session.coarse_kp_best += step_kp;
            } else {
                g_session.coarse_kd_best += step_kd;
            }
        } else {
            // Slow improvement, try different approach
            g_session.exploration_factor = 0.6f;
            if (prev_drop->overthrow_percent > g_config.max_overthrow_percent) {
                // Reduce Kp, increase Kd
                g_session.coarse_kp_best -= step_kp;
                g_session.coarse_kd_best += step_kd;
            } else if (prev_drop->overthrow_percent < 1.0f) {
                // Too conservative, push Kp higher
                g_session.coarse_kp_best += step_kp * 1.5f;
            } else {
                // Balanced, tune Kd
                g_session.coarse_kd_best += step_kd;
            }
        }
    }
    else {
        // Drop 4+: Advanced optimization with momentum
        ai_drop_telemetry_t* curr = &g_session.drops[drops_in_phase - 1];
        ai_drop_telemetry_t* prev = &g_session.drops[drops_in_phase - 2];

        // If performance is excellent, reduce exploration
        if (curr->overall_score > 85.0f) {
            g_session.exploration_factor = fmaxf(0.1f, g_session.exploration_factor - 0.2f);
        }

        // Gradient-based adjustment
        if (curr->overall_score < prev->overall_score - 2.0f) {
            // Getting worse, reverse direction
            g_session.coarse_kp_best -= step_kp;
            g_session.coarse_kd_best += step_kd;
            g_session.exploration_factor += 0.1f;  // More exploration
        } else if (curr->overall_score > prev->overall_score) {
            // Improving! Continue but reduce exploration
            g_session.exploration_factor = fmaxf(0.1f, g_session.exploration_factor - 0.1f);
            if (curr->overthrow_percent > g_config.max_overthrow_percent) {
                g_session.coarse_kd_best += step_kd;
            } else if (curr->total_time_ms > g_config.target_coarse_time_ms) {
                g_session.coarse_kp_best += step_kp;
            }
        }
    }

    // Clamp to valid ranges
    g_session.coarse_kp_best = fmaxf(g_session.coarse_kp_min, fminf(g_session.coarse_kp_best, g_session.coarse_kp_max));
    g_session.coarse_kd_best = fmaxf(g_session.coarse_kd_min, fminf(g_session.coarse_kd_best, g_session.coarse_kd_max));
}

static void calculate_next_params_phase2(void) {
    // RP2350 Enhanced: Bayesian-inspired fine trickler tuning
    // Fine trickler requires more precision, so use smaller base steps
    uint8_t total_drops = g_session.drops_completed;

    // Find phase 2 start (first drop after phase 1 completed)
    uint8_t phase2_start = 0;
    for (uint8_t i = 0; i < total_drops; i++) {
        if (g_session.drops[i].fine_kp_used == g_session.fine_kp_best) {
            phase2_start = i;
            break;
        }
    }
    uint8_t drops_in_phase = total_drops - phase2_start;

    ai_drop_telemetry_t* prev_drop = total_drops > 0 ? &g_session.drops[total_drops - 1] : NULL;

    // Fine trickler needs precision: smaller base step
    float base_step = 0.1f;
    float step_kp = base_step * (0.8f + g_session.exploration_factor * 0.6f);
    float step_kd = base_step * (0.8f + g_session.exploration_factor * 0.4f);

    if (drops_in_phase == 1) {
        // First fine drop: intelligent exploration based on coarse performance
        if (prev_drop && prev_drop->overall_score > 85.0f) {
            // Coarse was excellent! Fine-tune gently
            g_session.fine_kp_best += step_kp * 0.5f;
            g_session.exploration_factor = 0.3f;  // Low exploration
        } else if (prev_drop && prev_drop->overall_score < 75.0f) {
            // Coarse struggled, fine needs more work
            g_session.fine_kp_best += step_kp * 1.5f;
            g_session.exploration_factor = 0.6f;  // More exploration
        } else {
            // Normal exploration
            g_session.fine_kp_best += step_kp;
            g_session.exploration_factor = 0.4f;
        }
    }
    else if (drops_in_phase == 2) {
        // Second fine drop: adjust based on first fine result
        ai_drop_telemetry_t* fine_drop1 = &g_session.drops[phase2_start];
        ai_drop_telemetry_t* fine_drop2 = &g_session.drops[phase2_start + 1];

        // RP2350: Double precision gradient
        double score_gradient = (double)fine_drop2->overall_score - (double)fine_drop1->overall_score;

        if (fine_drop2->overall_score > 90.0f) {
            // Outstanding! Minimal tuning needed
            g_session.exploration_factor = 0.1f;
            if (fine_drop2->overthrow_percent > 1.0f) {
                g_session.fine_kd_best += step_kd * 0.3f;
            }
        } else if (score_gradient > 5.0) {
            // Improving well, continue direction
            g_session.exploration_factor = 0.2f;
            if (prev_drop->overthrow_percent < g_config.max_overthrow_percent / 2.0f) {
                g_session.fine_kp_best += step_kp * 0.8f;
            } else {
                g_session.fine_kd_best += step_kd * 0.8f;
            }
        } else {
            // Needs more tuning
            g_session.exploration_factor = 0.5f;
            if (prev_drop->overthrow_percent > g_config.max_overthrow_percent) {
                g_session.fine_kp_best -= step_kp;
                g_session.fine_kd_best += step_kd;
            } else if (prev_drop->overthrow_percent < 0.5f) {
                g_session.fine_kp_best += step_kp * 1.2f;
            } else {
                g_session.fine_kd_best += step_kd;
            }
        }
    }
    else {
        // Drop 3+: Advanced fine-tuning
        ai_drop_telemetry_t* curr = &g_session.drops[total_drops - 1];
        ai_drop_telemetry_t* prev = &g_session.drops[total_drops - 2];

        // Excellent performance → minimal exploration
        if (curr->overall_score > 90.0f) {
            g_session.exploration_factor = fmaxf(0.05f, g_session.exploration_factor - 0.15f);
        }

        if (curr->overall_score < prev->overall_score - 2.0f) {
            // Getting worse, reverse
            g_session.fine_kp_best -= step_kp * 0.8f;
            g_session.fine_kd_best += step_kd * 0.8f;
            g_session.exploration_factor += 0.1f;
        } else if (curr->overall_score > prev->overall_score) {
            // Improving, reduce exploration
            g_session.exploration_factor = fmaxf(0.05f, g_session.exploration_factor - 0.1f);
            if (curr->overthrow_percent > g_config.max_overthrow_percent * 0.8f) {
                g_session.fine_kd_best += step_kd * 0.6f;
            } else if (curr->fine_time_ms > (g_config.target_total_time_ms - g_config.target_coarse_time_ms)) {
                g_session.fine_kp_best += step_kp * 0.6f;
            }
        }
    }

    // Clamp to valid ranges
    g_session.fine_kp_best = fmaxf(g_session.fine_kp_min, fminf(g_session.fine_kp_best, g_session.fine_kp_max));
    g_session.fine_kd_best = fmaxf(g_session.fine_kd_min, fminf(g_session.fine_kd_best, g_session.fine_kd_max));
}

static void finalize_recommendations(void) {
    // RP2350: Calculate statistics with double precision accumulation
    // Takes advantage of fast DCP (2-3 cycles/op)
    double total_overthrow = 0.0;
    double total_time = 0.0;
    float max_overthrow = 0.0f;
    float min_overthrow = 999.0f;

    // Use actual drops_completed instead of total_drops_target
    uint8_t drops_to_analyze = g_session.drops_completed;

    for (uint8_t i = 0; i < drops_to_analyze; i++) {
        // Double precision accumulation reduces floating-point error
        total_overthrow += (double)fabsf(g_session.drops[i].overthrow_percent);
        total_time += (double)g_session.drops[i].total_time_ms;

        float overthrow_abs = fabsf(g_session.drops[i].overthrow_percent);
        if (overthrow_abs > max_overthrow) max_overthrow = overthrow_abs;
        if (overthrow_abs < min_overthrow) min_overthrow = overthrow_abs;
    }

    // Convert back to float for storage (maintains precision in calculation)
    g_session.avg_overthrow = (float)(total_overthrow / (double)drops_to_analyze);
    g_session.avg_total_time = (float)(total_time / (double)drops_to_analyze);

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
