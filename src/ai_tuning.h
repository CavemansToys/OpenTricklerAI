#ifndef AI_TUNING_H_
#define AI_TUNING_H_

#include <stdint.h>
#include <stdbool.h>
#include "profile.h"

/**
 * AI-Powered PID Auto-Tuning System (RP2350 Enhanced)
 *
 * Automatically tunes Kp and Kd parameters for both coarse and fine tricklers
 * using intelligent adaptive optimization. Targets 4-6 drops but adapts as needed.
 *
 * RP2350 Enhancements:
 * - Double-precision accumulation (fast DCP, 2-3 cycles/op)
 * - Bayesian-inspired parameter selection (hardware FPU, 6x faster)
 * - Smart convergence detection (fewer drops needed)
 * - Extended history (50 drops max, leveraging 520KB RAM)
 * - FMA optimization for cost function (single-cycle precision)
 *
 * Algorithm:
 * - Phase 1: Tune coarse trickler (adaptive, target 2-3 drops)
 * - Phase 2: Tune fine trickler (adaptive, target 2-3 drops)
 * - Uses Bayesian-inspired exploration/exploitation
 * - Smart convergence: stops early if performance is good
 * - Cost = α(overthrow) + β(time) + γ(consistency)
 *
 * Usage:
 * 1. ai_tuning_start(profile) - Begin tuning session
 * 2. Call ai_tuning_record_drop() after each drop
 * 3. ai_tuning_get_recommended_params() when complete
 * 4. User confirms and applies parameters
 */

// Tuning state machine
typedef enum {
    AI_TUNING_IDLE = 0,
    AI_TUNING_PHASE_1_COARSE,     // Drops 1-5: Tune coarse trickler
    AI_TUNING_PHASE_2_FINE,       // Drops 6-10: Tune fine trickler
    AI_TUNING_COMPLETE,           // Tuning finished, awaiting confirmation
    AI_TUNING_ERROR               // Error occurred during tuning
} ai_tuning_state_t;

// Drop telemetry data collected during each drop
typedef struct {
    uint8_t drop_number;          // 1-10

    // Timing
    float coarse_time_ms;         // Time spent in coarse trickling
    float fine_time_ms;           // Time spent in fine trickling
    float total_time_ms;          // Total drop time

    // Accuracy
    float final_weight;           // Final weight achieved
    float target_weight;          // Target weight
    float overthrow;              // Amount of overthrow (negative = underthrow)
    float overthrow_percent;      // Overthrow as percentage of target

    // PID values used for this drop
    float coarse_kp_used;
    float coarse_kd_used;
    float fine_kp_used;
    float fine_kd_used;

    // Quality metrics
    float accuracy_score;         // 0-100, higher is better
    float speed_score;            // 0-100, higher is better
    float overall_score;          // Weighted combination
} ai_drop_telemetry_t;

// AI tuning session state
typedef struct {
    ai_tuning_state_t state;

    profile_t* target_profile;    // Profile being tuned

    // Progress
    uint8_t drops_completed;      // Current drop count
    uint8_t total_drops_target;   // Initial target (adaptive: 4-6 drops)
    uint8_t max_drops_allowed;    // Maximum drops (safety limit: 50)
    uint8_t min_drops_per_phase;  // Minimum drops per phase before checking convergence

    // Telemetry history (RP2350: 50 drops with 520KB RAM)
    ai_drop_telemetry_t drops[50]; // Extended history for better learning

    // Algorithm state - Phase 1: Coarse tuning
    float coarse_kp_best;
    float coarse_kd_best;
    float coarse_best_score;
    float coarse_kp_min;
    float coarse_kp_max;
    float coarse_kd_min;
    float coarse_kd_max;

    // Algorithm state - Phase 2: Fine tuning
    float fine_kp_best;
    float fine_kd_best;
    float fine_best_score;
    float fine_kp_min;
    float fine_kp_max;
    float fine_kd_min;
    float fine_kd_max;

    // Recommended final values
    float recommended_coarse_kp;
    float recommended_coarse_kd;
    float recommended_fine_kp;
    float recommended_fine_kd;

    // Statistics (RP2350: double precision for accumulation)
    float avg_overthrow;
    float avg_total_time;
    float consistency_score;      // Lower variance = higher consistency

    // RP2350 Enhanced: Exploration/exploitation tracking
    float exploration_factor;     // How much to explore vs exploit (0.0-1.0)
    uint8_t consecutive_good_drops; // Track streak of good performance

    // Error handling
    char error_message[64];
} ai_tuning_session_t;

// Configuration
typedef struct {
    // Target performance goals
    float max_overthrow_percent;      // Maximum acceptable overthrow (default: 6.67% = 1/15)
    float target_coarse_time_ms;      // Target coarse time (default: 10000ms)
    float target_total_time_ms;       // Target total time (default: 15000ms)

    // Cost function weights
    float weight_overthrow;           // Weight for overthrow in cost function (default: 10.0)
    float weight_time;                // Weight for time in cost function (default: 1.0)
    float weight_consistency;         // Weight for consistency in cost function (default: 5.0)

    // Parameter search ranges (match app's built-in PID validation: 0.0 - 100.0)
    float coarse_kp_min;              // Min coarse Kp to explore (default: 0.0)
    float coarse_kp_max;              // Max coarse Kp to explore (default: 100.0)
    float coarse_kd_min;              // Min coarse Kd to explore (default: 0.0)
    float coarse_kd_max;              // Max coarse Kd to explore (default: 100.0)

    float fine_kp_min;                // Min fine Kp to explore (default: 0.0)
    float fine_kp_max;                // Max fine Kp to explore (default: 100.0)
    float fine_kd_min;                // Min fine Kd to explore (default: 0.0)
    float fine_kd_max;                // Max fine Kd to explore (default: 100.0)

    // Learning rate for gradient descent
    float learning_rate_kp;           // Learning rate for Kp adjustment (default: 0.1)
    float learning_rate_kd;           // Learning rate for Kd adjustment (default: 0.1)

} ai_tuning_config_t;


#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize AI tuning system with default configuration
 */
void ai_tuning_init(void);

/**
 * Get current tuning configuration
 */
ai_tuning_config_t* ai_tuning_get_config(void);

/**
 * Start a new tuning session for the given profile
 * @param profile Profile to tune
 * @return true if session started successfully
 */
bool ai_tuning_start(profile_t* profile);

/**
 * Record telemetry data from a completed drop
 * Called by charge_mode after each drop completes
 * @param telemetry Drop telemetry data
 * @return true if recorded successfully
 */
bool ai_tuning_record_drop(const ai_drop_telemetry_t* telemetry);

/**
 * Get parameters to use for the next drop
 * Called by charge_mode before starting a drop
 * @param coarse_kp Output: Kp value for coarse trickler
 * @param coarse_kd Output: Kd value for coarse trickler
 * @param fine_kp Output: Kp value for fine trickler
 * @param fine_kd Output: Kd value for fine trickler
 * @return true if parameters are available
 */
bool ai_tuning_get_next_params(float* coarse_kp, float* coarse_kd,
                                 float* fine_kp, float* fine_kd);

/**
 * Check if tuning session is complete
 */
bool ai_tuning_is_complete(void);

/**
 * Get current tuning session state
 */
ai_tuning_session_t* ai_tuning_get_session(void);

/**
 * Get recommended parameters after tuning completes
 * @param coarse_kp Output: Recommended coarse Kp
 * @param coarse_kd Output: Recommended coarse Kd
 * @param fine_kp Output: Recommended fine Kp
 * @param fine_kd Output: Recommended fine Kd
 * @return true if recommendations available
 */
bool ai_tuning_get_recommended_params(float* coarse_kp, float* coarse_kd,
                                       float* fine_kp, float* fine_kd);

/**
 * Apply recommended parameters to the profile
 * Called after user confirms the recommendations
 * @return true if applied successfully
 */
bool ai_tuning_apply_params(void);

/**
 * Cancel current tuning session
 */
void ai_tuning_cancel(void);

/**
 * Check if tuning session is active
 */
bool ai_tuning_is_active(void);

/**
 * Get progress percentage (0-100)
 */
uint8_t ai_tuning_get_progress_percent(void);

/**
 * Calculate cost function for given parameters and results
 * Lower cost is better
 */
float ai_tuning_calculate_cost(float overthrow, float time_ms, float variance);

#ifdef __cplusplus
}
#endif

#endif  // AI_TUNING_H_
