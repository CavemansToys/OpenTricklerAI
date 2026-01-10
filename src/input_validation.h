#ifndef INPUT_VALIDATION_H_
#define INPUT_VALIDATION_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "lwip/apps/fs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Validation result structure
typedef struct {
    bool is_valid;
    const char* error_message;
} validation_result_t;

// Constants for validation
#define VALIDATION_OK ((validation_result_t){.is_valid = true, .error_message = NULL})
#define VALIDATION_ERROR(msg) ((validation_result_t){.is_valid = false, .error_message = msg})

// Motor validation constants
#define MOTOR_MIN_ANGULAR_ACCEL     0.1f
#define MOTOR_MAX_ANGULAR_ACCEL     500.0f
#define MOTOR_MIN_CURRENT_MA        100
#define MOTOR_MAX_CURRENT_MA        2000
#define MOTOR_MIN_MICROSTEPS        1
#define MOTOR_MAX_MICROSTEPS        256
#define MOTOR_MIN_SPEED_RPS         0.0f
#define MOTOR_MAX_SPEED_RPS         50.0f
#define MOTOR_MIN_GEAR_RATIO        0.1f
#define MOTOR_MAX_GEAR_RATIO        10.0f
#define MOTOR_MIN_RSENSE            10
#define MOTOR_MAX_RSENSE            500

// Charge mode validation constants
#define CHARGE_MIN_THRESHOLD        0.001f
#define CHARGE_MAX_THRESHOLD        1000.0f
#define CHARGE_MIN_MARGIN           0.0f
#define CHARGE_MAX_MARGIN           10.0f
#define CHARGE_MIN_PRECHARGE_TIME   0
#define CHARGE_MAX_PRECHARGE_TIME   60000
#define CHARGE_MIN_TARGET_WEIGHT    0.0f
#define CHARGE_MAX_TARGET_WEIGHT    10000.0f

// Servo validation constants
#define SERVO_MIN_DUTY_CYCLE_FRAC   0.0f
#define SERVO_MAX_DUTY_CYCLE_FRAC   1.0f
#define SERVO_MIN_SPEED             0.1f
#define SERVO_MAX_SPEED             100.0f

// Cleanup mode validation constants
#define CLEANUP_MIN_SPEED           -50.0f
#define CLEANUP_MAX_SPEED           50.0f

// PID validation constants
#define PID_MIN_KP                  0.0f
#define PID_MAX_KP                  100.0f
#define PID_MIN_KI                  0.0f
#define PID_MAX_KI                  100.0f
#define PID_MIN_KD                  0.0f
#define PID_MAX_KD                  100.0f
#define PID_MIN_FLOW_SPEED          0.0f
#define PID_MAX_FLOW_SPEED          50.0f

// Profile validation constants
#define PROFILE_MAX_INDEX           7
#define PROFILE_NAME_MIN_LEN        1
#define PROFILE_NAME_MAX_LEN        16

// Scale configuration validation constants
#define SCALE_MIN_DRIVER_INDEX      0
#define SCALE_MAX_DRIVER_INDEX      6   // 7 scale drivers (0-6)
#define SCALE_MIN_BAUDRATE_INDEX    0
#define SCALE_MAX_BAUDRATE_INDEX    2   // 3 baudrates (0-2)

// Neopixel LED validation constants
#define NEOPIXEL_MIN_CHAIN_COUNT    0
#define NEOPIXEL_MAX_CHAIN_COUNT    16
#define NEOPIXEL_MIN_COLOUR_ORDER   0
#define NEOPIXEL_MAX_COLOUR_ORDER   1

// Display validation constants
#define DISPLAY_MIN_ROTATION        0
#define DISPLAY_MAX_ROTATION        3   // 4 rotations (0-3)

// Wireless validation constants
#define WIRELESS_MIN_AUTH_TYPE      0
#define WIRELESS_MAX_AUTH_TYPE      3   // 4 auth types (0-3)
#define WIRELESS_MIN_TIMEOUT_MS     1000
#define WIRELESS_MAX_TIMEOUT_MS     60000

// Generic validation functions
static inline bool is_valid_float(float value) {
    return !isnan(value) && !isinf(value);
}

static inline bool is_in_range_float(float value, float min, float max) {
    return is_valid_float(value) && value >= min && value <= max;
}

static inline bool is_in_range_int(int value, int min, int max) {
    return value >= min && value <= max;
}

static inline bool is_in_range_uint(uint32_t value, uint32_t min, uint32_t max) {
    return value >= min && value <= max;
}

// Motor configuration validation
static inline validation_result_t validate_angular_acceleration(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid angular acceleration (NaN/Inf)");
    if (!is_in_range_float(value, MOTOR_MIN_ANGULAR_ACCEL, MOTOR_MAX_ANGULAR_ACCEL))
        return VALIDATION_ERROR("Angular acceleration out of range (0.1-500 rev/s^2)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_motor_current(uint16_t value) {
    if (!is_in_range_int(value, MOTOR_MIN_CURRENT_MA, MOTOR_MAX_CURRENT_MA))
        return VALIDATION_ERROR("Motor current out of range (100-2000 mA)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_microsteps(uint16_t value) {
    // Microsteps must be power of 2
    if (value < MOTOR_MIN_MICROSTEPS || value > MOTOR_MAX_MICROSTEPS)
        return VALIDATION_ERROR("Microsteps out of range (1-256)");
    // Check if power of 2
    if ((value & (value - 1)) != 0)
        return VALIDATION_ERROR("Microsteps must be power of 2");
    return VALIDATION_OK;
}

static inline validation_result_t validate_motor_speed(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid motor speed (NaN/Inf)");
    if (!is_in_range_float(value, MOTOR_MIN_SPEED_RPS, MOTOR_MAX_SPEED_RPS))
        return VALIDATION_ERROR("Motor speed out of range (0-50 rev/s)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_min_speed(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid min speed (NaN/Inf)");
    if (!is_in_range_float(value, MOTOR_MIN_SPEED_RPS, MOTOR_MAX_SPEED_RPS))
        return VALIDATION_ERROR("Min speed out of range (0-50 rev/s)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_gear_ratio(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid gear ratio (NaN/Inf)");
    if (!is_in_range_float(value, MOTOR_MIN_GEAR_RATIO, MOTOR_MAX_GEAR_RATIO))
        return VALIDATION_ERROR("Gear ratio out of range (0.1-10)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_rsense(uint16_t value) {
    if (!is_in_range_int(value, MOTOR_MIN_RSENSE, MOTOR_MAX_RSENSE))
        return VALIDATION_ERROR("R-sense out of range (10-500 mOhm)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_full_steps(uint32_t value) {
    // Common stepper motor values: 200 (1.8°) or 400 (0.9°)
    if (value != 200 && value != 400)
        return VALIDATION_ERROR("Full steps must be 200 or 400");
    return VALIDATION_OK;
}

// Charge mode validation
static inline validation_result_t validate_threshold(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid threshold (NaN/Inf)");
    if (!is_in_range_float(value, CHARGE_MIN_THRESHOLD, CHARGE_MAX_THRESHOLD))
        return VALIDATION_ERROR("Threshold out of range (0.001-1000)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_margin(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid margin (NaN/Inf)");
    if (!is_in_range_float(value, CHARGE_MIN_MARGIN, CHARGE_MAX_MARGIN))
        return VALIDATION_ERROR("Margin out of range (0-10)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_precharge_time(uint32_t value) {
    if (!is_in_range_uint(value, CHARGE_MIN_PRECHARGE_TIME, CHARGE_MAX_PRECHARGE_TIME))
        return VALIDATION_ERROR("Precharge time out of range (0-60000 ms)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_target_weight(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid target weight (NaN/Inf)");
    if (!is_in_range_float(value, CHARGE_MIN_TARGET_WEIGHT, CHARGE_MAX_TARGET_WEIGHT))
        return VALIDATION_ERROR("Target weight out of range (0-10000)");
    return VALIDATION_OK;
}

// Cleanup mode validation
static inline validation_result_t validate_cleanup_speed(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid cleanup speed (NaN/Inf)");
    if (!is_in_range_float(value, CLEANUP_MIN_SPEED, CLEANUP_MAX_SPEED))
        return VALIDATION_ERROR("Cleanup speed out of range (-50 to 50 rev/s)");
    return VALIDATION_OK;
}

// Servo gate validation
static inline validation_result_t validate_servo_duty_cycle(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid servo duty cycle (NaN/Inf)");
    if (!is_in_range_float(value, SERVO_MIN_DUTY_CYCLE_FRAC, SERVO_MAX_DUTY_CYCLE_FRAC))
        return VALIDATION_ERROR("Servo duty cycle out of range (0.0-1.0)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_servo_speed(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid servo speed (NaN/Inf)");
    if (!is_in_range_float(value, SERVO_MIN_SPEED, SERVO_MAX_SPEED))
        return VALIDATION_ERROR("Servo speed out of range (0.1-100 %/s)");
    return VALIDATION_OK;
}

// PID parameter validation
static inline validation_result_t validate_pid_kp(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid Kp value (NaN/Inf)");
    if (!is_in_range_float(value, PID_MIN_KP, PID_MAX_KP))
        return VALIDATION_ERROR("Kp out of range (0.0-100.0)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_pid_ki(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid Ki value (NaN/Inf)");
    if (!is_in_range_float(value, PID_MIN_KI, PID_MAX_KI))
        return VALIDATION_ERROR("Ki out of range (0.0-100.0)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_pid_kd(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid Kd value (NaN/Inf)");
    if (!is_in_range_float(value, PID_MIN_KD, PID_MAX_KD))
        return VALIDATION_ERROR("Kd out of range (0.0-100.0)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_flow_speed(float value) {
    if (!is_valid_float(value))
        return VALIDATION_ERROR("Invalid flow speed (NaN/Inf)");
    if (!is_in_range_float(value, PID_MIN_FLOW_SPEED, PID_MAX_FLOW_SPEED))
        return VALIDATION_ERROR("Flow speed out of range (0.0-50.0 rev/s)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_profile_index(uint8_t value) {
    if (value > PROFILE_MAX_INDEX)
        return VALIDATION_ERROR("Profile index out of range (0-7)");
    return VALIDATION_OK;
}

// Scale configuration validation
static inline validation_result_t validate_scale_driver(int value) {
    if (!is_in_range_int(value, SCALE_MIN_DRIVER_INDEX, SCALE_MAX_DRIVER_INDEX))
        return VALIDATION_ERROR("Scale driver out of range (0-6)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_scale_baudrate(int value) {
    if (!is_in_range_int(value, SCALE_MIN_BAUDRATE_INDEX, SCALE_MAX_BAUDRATE_INDEX))
        return VALIDATION_ERROR("Scale baudrate out of range (0-2)");
    return VALIDATION_OK;
}

// Neopixel LED validation
static inline validation_result_t validate_led_chain_count(int value) {
    if (!is_in_range_int(value, NEOPIXEL_MIN_CHAIN_COUNT, NEOPIXEL_MAX_CHAIN_COUNT))
        return VALIDATION_ERROR("LED chain count out of range (0-16)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_led_colour_order(int value) {
    if (!is_in_range_int(value, NEOPIXEL_MIN_COLOUR_ORDER, NEOPIXEL_MAX_COLOUR_ORDER))
        return VALIDATION_ERROR("LED colour order out of range (0-1)");
    return VALIDATION_OK;
}

// Display validation
static inline validation_result_t validate_display_rotation(int value) {
    if (!is_in_range_int(value, DISPLAY_MIN_ROTATION, DISPLAY_MAX_ROTATION))
        return VALIDATION_ERROR("Display rotation out of range (0-3)");
    return VALIDATION_OK;
}

// Wireless validation
static inline validation_result_t validate_wireless_auth(int value) {
    if (!is_in_range_int(value, WIRELESS_MIN_AUTH_TYPE, WIRELESS_MAX_AUTH_TYPE))
        return VALIDATION_ERROR("Wireless auth type out of range (0-3)");
    return VALIDATION_OK;
}

static inline validation_result_t validate_wireless_timeout(uint32_t value) {
    if (!is_in_range_uint(value, WIRELESS_MIN_TIMEOUT_MS, WIRELESS_MAX_TIMEOUT_MS))
        return VALIDATION_ERROR("Wireless timeout out of range (1000-60000 ms)");
    return VALIDATION_OK;
}

// Helper function to send error response
static inline bool send_validation_error(struct fs_file *file, const char* error_message) {
    static char error_buffer[256];

    int len = snprintf(error_buffer, sizeof(error_buffer),
                      "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Type: application/json\r\n"
                      "\r\n"
                      "{\"error\":\"validation_failed\",\"message\":\"%s\"}",
                      error_message);

    if (len < 0 || len >= (int)sizeof(error_buffer)) {
        // Truncation occurred, send generic error
        file->data = "HTTP/1.1 400 Bad Request\r\n"
                    "Content-Type: application/json\r\n"
                    "\r\n"
                    "{\"error\":\"validation_failed\",\"message\":\"Invalid input\"}";
        file->len = strlen(file->data);
    } else {
        file->data = error_buffer;
        file->len = len;
    }

    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return false;  // Indicate validation failure
}

// Helper function to send buffer overflow error
static inline bool send_buffer_overflow_error(struct fs_file *file) {
    file->data = "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: application/json\r\n"
                "\r\n"
                "{\"error\":\"buffer_overflow\",\"message\":\"Response too large\"}";
    file->len = strlen(file->data);
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return false;
}

// Macro to check snprintf return value for overflow
#define CHECK_SNPRINTF_OVERFLOW(len, buffer_size, file) \
    do { \
        if ((len) < 0 || (len) >= (int)(buffer_size)) { \
            return send_buffer_overflow_error(file); \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif  // INPUT_VALIDATION_H_
