/*
 * LED blink with FreeRTOS
 */

#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "FreeRTOSConfig.h"
#include "configuration.h"
#include "u8g2.h"

// modules
#include "app.h"
#include "motors.h"
#include "eeprom.h"
#include "scale.h"
#include "display.h"
#include "charge_mode.h"
#include "rest_endpoints.h"
#include "wireless.h"
#include "neopixel_led.h"
#include "mini_12864_module.h"
#include "menu.h"
#include "profile.h"
#include "servo_gate.h"

// OTA firmware update
#include "firmware_update/firmware_manager.h"
#include "firmware_update/firmware_upload.h"
#include "firmware_update/firmware_download.h"
#include "firmware_update/rest_firmware.h"

// AI PID auto-tuning
#include "ai_tuning.h"
#include "rest_ai_tuning.h"

// Watchdog timer configuration
#define WATCHDOG_TIMEOUT_MS 8000  // 8 second timeout

#ifdef OTA_TEST_MODE
// Simple test task for OTA testing on bare board
void ota_test_task(void *pvParameters) {
    (void)pvParameters;

    uint32_t counter = 0;

    printf("OTA Test Task started\n");
    printf("Access the device at: http://opentrickler.local\n");
    printf("REST API endpoints available:\n");
    printf("  GET  /rest/firmware_status\n");
    printf("  POST /upload (with firmware binary)\n");
    printf("  GET  /rest/firmware_download?url=<url>\n");
    printf("  POST /rest/firmware_activate\n");
    printf("  POST /rest/firmware_rollback\n");
    printf("  POST /rest/firmware_cancel\n");
    printf("\n");

    while (true) {
        // Update watchdog to prevent reset
        watchdog_update();

        // Print status every 30 seconds
        if (counter % 30 == 0) {
            printf("[%lu] OTA system running, waiting for commands...\n", counter);
            firmware_bank_t current_bank = firmware_manager_get_current_bank();
            printf("Current bank: %s\n", (current_bank == BANK_A) ? "A" : "B");
        }

        counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 second delay
    }
}
#endif

int main()
{
    stdio_init_all();

    // CRITICAL: Wait for USB serial to enumerate before printing
    // Without this delay, early printf() output is lost
    sleep_ms(2000);

    printf("\n\n");
    printf("==========================================\n");
    printf("OpenTrickler RP2350 - Firmware Starting\n");
    printf("==========================================\n");
    printf("Pico SDK initialized\n");

    // Enable watchdog timer for automatic recovery from hangs
    // Must be updated periodically or system will reset
    if (watchdog_caused_reboot()) {
        printf("WARNING: System recovered from watchdog reset\n");
    }
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    printf("Watchdog enabled (%d ms timeout)\n", WATCHDOG_TIMEOUT_MS);

    // Initialize EEPROM first (required for wireless config)
    eeprom_init();
    printf("EEPROM initialized\n");

#ifndef OTA_TEST_MODE
    // Initialize Neopixel RGB on the mini 12864 board
    neopixel_led_init();

    // Configure other functions from mini 12864 display
    mini_12864_module_init();

    // Initialize wireless settings
    wireless_init();
#else
    printf("\n");
    printf("==============================================\n");
    printf("OTA TEST MODE - Bare Board Testing\n");
    printf("==============================================\n");
    printf("Hardware peripherals disabled for testing\n");
    printf("WiFi initialization SKIPPED to allow USB serial\n");
    printf("USB serial conflicts with lwIP/WiFi stack\n");
    printf("\n");
    printf("LED should be SOLID ON\n");
    printf("\n");

    // Skip WiFi init in test mode - it kills USB serial
    // Just blink the onboard LED to show we're alive
    printf("Blinking LED every 1 second...\n");
#endif

#ifndef OTA_TEST_MODE
    // Load config for motors
    motor_init_err_t motor_init_err = motors_init();
    if (motor_init_err != MOTOR_INIT_OK) {
        handle_motor_init_error(motor_init_err);
    }

    // Initialize UART
    scale_init();

    // Initialize charge mode settings
    charge_mode_config_init();

    // Initialize profile data
    profile_data_init();

    // Initialize the servo
    servo_gate_init();
#endif

    //===========================================================================
    // OTA Firmware Update System
    //===========================================================================
    printf("\n==============================================\n");
    printf("OTA Firmware Update System\n");
    printf("==============================================\n");

    // Initialize firmware manager
    if (!firmware_manager_init()) {
        printf("ERROR: Failed to initialize firmware manager\n");
    } else {
        // Check if rollback occurred
        if (firmware_manager_did_rollback_occur()) {
            printf("WARNING: *** FIRMWARE ROLLBACK OCCURRED ***\n");
            printf("The previous firmware failed to boot properly.\n");
            printf("System automatically rolled back to this firmware.\n");
            printf("Please check logs for errors before updating again.\n");

            // Clear rollback flag after displaying warning
            firmware_manager_clear_rollback_flag();
        }

        // Get current bank info
        firmware_bank_t current_bank = firmware_manager_get_current_bank();
        firmware_info_t bank_info;
        if (firmware_manager_get_bank_info(current_bank, &bank_info)) {
            printf("Running from: Bank %s\n", (current_bank == BANK_A) ? "A" : "B");
            printf("Version: %s\n", bank_info.version);
            printf("Size: %lu bytes\n", bank_info.size);
            printf("CRC32: 0x%08lx\n", bank_info.crc32);
        }

        // Initialize firmware upload handler
        firmware_upload_init();

        // Initialize firmware download handler
        firmware_download_init();

        // Initialize REST API endpoints
        rest_firmware_init();

        // Initialize AI tuning system
        ai_tuning_init();

        // Initialize AI tuning REST API endpoints
        rest_ai_tuning_init();

        // Confirm successful boot (resets boot counter)
        // This must be called after all critical initialization is complete
        printf("Confirming successful boot...\n");
        if (firmware_manager_confirm_boot()) {
            printf("Boot confirmed - boot counter reset\n");
        } else {
            printf("WARNING: Failed to confirm boot\n");
        }
    }

    printf("==============================================\n\n");
    //===========================================================================

#ifdef OTA_TEST_MODE
    // Start OTA test task for bare board testing
    printf("Starting OTA test task...\n");
    xTaskCreate(ota_test_task, "OTA Test", 2048, NULL, 5, NULL);
#else
    // Start menu task (highest priority task should update watchdog)
    xTaskCreate(menu_task, "Menu Task", 1024, NULL, 6, NULL);
#endif

    // Start RTOS
    vTaskStartScheduler();

    while (true)
        ;
}
