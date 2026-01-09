#include "metadata.h"
#include "flash_partitions.h"
#include "../firmware_update/flash_ops.h"
#include "../firmware_update/crc32.h"
#include "hardware/structs/scb.h"
#include "pico/stdlib.h"
#include "pico/platform.h"
#include <stdio.h>
#include <string.h>

// LED pin definition for error indication
// Pico W/2W use CYW43 wireless chip LED, not direct GPIO
// For bootloader simplicity, we'll skip LED blinking if not available
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25  // Fallback for non-wireless boards
#define LED_NOT_AVAILABLE 1
#endif

/**
 * Dual-Bank OTA Bootloader for OpenTrickler
 *
 * Boot Sequence:
 * 1. Read metadata (double-buffered, validated)
 * 2. Determine active firmware bank
 * 3. Check boot counter < MAX_BOOT_ATTEMPTS
 * 4. If exceeded, trigger automatic rollback
 * 5. Validate firmware CRC32
 * 6. If invalid, try opposite bank
 * 7. Increment boot counter and update metadata
 * 8. Set VTOR and jump to firmware
 *
 * Safety Features:
 * - CRC32 validation before boot
 * - Automatic rollback after 3 failed boots
 * - Dual-bank ensures always have working firmware
 * - Metadata double-buffering prevents corruption
 */

// LED blink patterns for error indication
#define BLINK_PATTERN_NO_VALID_FIRMWARE     5   // 5 short, 1 long
#define BLINK_PATTERN_BOTH_BANKS_FAILED     3   // 3 short, 1 long

// Bootloader version
#define BOOTLOADER_VERSION "1.0.0"

/**
 * Blink LED to indicate fatal error
 * Never returns - system halts
 */
static void __attribute__((noreturn)) panic_blink(int pattern_count) {
#ifndef LED_NOT_AVAILABLE
    // Initialize GPIO for LED (typically GPIO 25 on Pico)
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
#endif

    while (true) {
#ifndef LED_NOT_AVAILABLE
        // Short blinks
        for (int i = 0; i < pattern_count; i++) {
            gpio_put(LED_PIN, 1);
            sleep_ms(200);
            gpio_put(LED_PIN, 0);
            sleep_ms(200);
        }

        // Long blink
        gpio_put(LED_PIN, 1);
        sleep_ms(1000);
        gpio_put(LED_PIN, 0);
        sleep_ms(1000);
#else
        // No LED available, just loop with delay
        sleep_ms(2000);
#endif
    }
}

/**
 * Validate firmware in specified bank
 */
static bool validate_firmware_bank(firmware_bank_t bank, const firmware_metadata_t *meta) {
    uint32_t expected_crc32;
    uint32_t expected_size;
    uint8_t valid_flag;

    // Get bank info from metadata
    if (bank == BANK_A) {
        expected_crc32 = meta->bank_a_crc32;
        expected_size = meta->bank_a_size;
        valid_flag = meta->bank_a_valid;
    } else if (bank == BANK_B) {
        expected_crc32 = meta->bank_b_crc32;
        expected_size = meta->bank_b_size;
        valid_flag = meta->bank_b_valid;
    } else {
        return false;
    }

    // Check valid flag
    if (valid_flag != BANK_VALID) {
        printf("Bank %s marked invalid\n", (bank == BANK_A) ? "A" : "B");
        return false;
    }

    // Check size is reasonable
    if (expected_size == 0 || expected_size > bank_get_size(bank)) {
        printf("Bank %s invalid size: %lu\n", (bank == BANK_A) ? "A" : "B", expected_size);
        return false;
    }

    // Validate CRC32
    uint32_t actual_crc32;
    flash_op_result_t result = flash_validate_firmware(bank, expected_crc32, expected_size,
                                                        &actual_crc32, NULL, NULL);

    if (result != FLASH_OP_SUCCESS) {
        printf("Bank %s CRC32 validation failed: expected=0x%08lx, actual=0x%08lx\n",
               (bank == BANK_A) ? "A" : "B", expected_crc32, actual_crc32);
        return false;
    }

    printf("Bank %s validated successfully\n", (bank == BANK_A) ? "A" : "B");
    return true;
}

/**
 * Jump to firmware application
 * Never returns
 */
static void __attribute__((noreturn)) jump_to_firmware(uint32_t firmware_address) {
    printf("Jumping to firmware at 0x%08lx\n\n", firmware_address);

    // Disable interrupts
    __disable_irq();

    // Set VTOR to firmware vector table
    scb_hw->vtor = firmware_address;

    // Get stack pointer and reset handler from vector table
    uint32_t *vector_table = (uint32_t *)firmware_address;
    uint32_t initial_sp = vector_table[0];
    uint32_t reset_handler = vector_table[1];

    // Set main stack pointer
    __asm volatile ("msr msp, %0" : : "r" (initial_sp) : "memory");

    // Jump to reset handler (firmware entry point)
    void (*app_reset_handler)(void) = (void(*)(void))(reset_handler);
    app_reset_handler();

    // Should never reach here
    while (1);
}

/**
 * Bootloader main entry point
 */
int main(void) {
    // Initialize stdio
    stdio_init_all();

    // Small delay to allow USB serial to connect (for debugging)
    sleep_ms(100);

    printf("\n\n");
    printf("==============================================\n");
    printf("OpenTrickler Dual-Bank OTA Bootloader v%s\n", BOOTLOADER_VERSION);
    printf("==============================================\n\n");

    // Initialize modules
    flash_ops_init();
    crc32_init();

    // Initialize metadata system
    if (!metadata_init()) {
        printf("FATAL: Failed to initialize metadata\n");
        panic_blink(BLINK_PATTERN_NO_VALID_FIRMWARE);
    }

    // Read current metadata
    firmware_metadata_t meta;
    if (!metadata_read(&meta)) {
        printf("FATAL: Failed to read metadata\n");
        panic_blink(BLINK_PATTERN_NO_VALID_FIRMWARE);
    }

    // Display metadata info
    printf("Active bank: %s\n", (meta.active_bank == BANK_A) ? "A" : "B");
    printf("Bank A: valid=%s, boot_count=%u, size=%lu, version=%s\n",
           (meta.bank_a_valid == BANK_VALID) ? "YES" : "NO",
           meta.bank_a_boot_count, meta.bank_a_size, meta.bank_a_version);
    printf("Bank B: valid=%s, boot_count=%u, size=%lu, version=%s\n",
           (meta.bank_b_valid == BANK_VALID) ? "YES" : "NO",
           meta.bank_b_boot_count, meta.bank_b_size, meta.bank_b_version);

    // Check for update in progress
    if (meta.update_in_progress == UPDATE_IN_PROGRESS) {
        printf("WARNING: Update was in progress, continuing with active bank\n");
        // Note: Application will clear this flag
    }

    firmware_bank_t boot_bank = meta.active_bank;
    uint8_t boot_count = 0;

    // Get boot count for active bank
    if (boot_bank == BANK_A) {
        boot_count = meta.bank_a_boot_count;
    } else if (boot_bank == BANK_B) {
        boot_count = meta.bank_b_boot_count;
    }

    // Check if boot count exceeded (automatic rollback trigger)
    if (boot_count >= MAX_BOOT_ATTEMPTS) {
        printf("WARNING: Boot count %u >= %u, triggering automatic rollback\n",
               boot_count, MAX_BOOT_ATTEMPTS);

        if (!metadata_trigger_rollback()) {
            printf("FATAL: Rollback failed\n");
            panic_blink(BLINK_PATTERN_BOTH_BANKS_FAILED);
        }

        // Re-read metadata after rollback
        if (!metadata_read(&meta)) {
            printf("FATAL: Failed to read metadata after rollback\n");
            panic_blink(BLINK_PATTERN_BOTH_BANKS_FAILED);
        }

        boot_bank = meta.active_bank;
    }

    // Validate active bank firmware
    bool active_valid = validate_firmware_bank(boot_bank, &meta);

    if (!active_valid) {
        printf("ERROR: Active bank %s validation failed, attempting rollback\n",
               (boot_bank == BANK_A) ? "A" : "B");

        // Try opposite bank
        if (!metadata_trigger_rollback()) {
            printf("FATAL: Rollback failed, no valid firmware available\n");
            panic_blink(BLINK_PATTERN_BOTH_BANKS_FAILED);
        }

        // Re-read metadata
        if (!metadata_read(&meta)) {
            printf("FATAL: Failed to read metadata after rollback\n");
            panic_blink(BLINK_PATTERN_BOTH_BANKS_FAILED);
        }

        boot_bank = meta.active_bank;

        // Validate new active bank
        if (!validate_firmware_bank(boot_bank, &meta)) {
            printf("FATAL: Both banks failed validation\n");
            panic_blink(BLINK_PATTERN_BOTH_BANKS_FAILED);
        }
    }

    // Increment boot counter
    printf("Incrementing boot counter for bank %s\n", (boot_bank == BANK_A) ? "A" : "B");
    if (!metadata_increment_boot_count()) {
        printf("WARNING: Failed to increment boot counter\n");
        // Continue anyway - application can retry
    }

    // Get firmware address
    uint32_t firmware_address = bank_get_address(boot_bank);

    printf("Booting firmware from bank %s\n", (boot_bank == BANK_A) ? "A" : "B");

    // Small delay to allow printf to complete
    sleep_ms(50);

    // Jump to firmware (never returns)
    jump_to_firmware(firmware_address);
}
