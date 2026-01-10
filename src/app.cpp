/*
 * LED blink with FreeRTOS
 */

#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
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

// Early initialization task - runs ALL init after FreeRTOS starts
void early_init_task(void *pvParameters) {
// Early initialization task - runs ALL init after FreeRTOS starts
void early_init_task(void *pvParameters) {
    (void)pvParameters;

    printf("Early init task starting...\n");
    
    // Initialize EEPROM (uses FreeRTOS mutex)
    eeprom_init();
    printf("EEPROM initialized\n");

    // Initialize cyw43 for LED and WiFi
    if (cyw43_arch_init() == 0) {
        printf("WiFi chip initialized\n");
        // Blink 5 times to show alive
        for (int i = 0; i < 5; i++) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wireless_mark_cyw43_initialized();
    }

    // Initialize wireless
    printf("Initializing wireless...\n");
    wireless_init();

    // Initialize OTA system
    printf("Initializing OTA firmware system...\n");
    if (!firmware_manager_init()) {
        printf("ERROR: Failed to initialize firmware manager\n");
    } else {
        firmware_upload_init();
        firmware_download_init();
        rest_firmware_init();
        ai_tuning_init();
        rest_ai_tuning_init();
        firmware_manager_confirm_boot();
        printf("OTA system initialized\n");
    }

    printf("Early init complete\n");
    vTaskDelete(NULL);
}
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

#ifndef OTA_TEST_MODE
    // Initialize Neopixel RGB on the mini 12864 board
    neopixel_led_init();

    // Configure other functions from mini 12864 display
    mini_12864_module_init();

#else
    printf("\n");
    printf("==============================================\n");
    printf("OTA TEST MODE - WiFi Testing\n");
    printf("==============================================\n");
    printf("LED Patterns:\n");
    printf("- Rapid blink (10x): WiFi chip initialized\n");
    printf("- Slow blink (1s): WiFi AP mode active\n");
    printf("- SOS pattern (...---...): Error occurred\n");
    printf("\n");

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


    // Start early init task (highest priority, runs first)
    printf("Starting FreeRTOS scheduler...\n");
    xTaskCreate(early_init_task, "Early Init", 2048, NULL, 10, NULL);

#ifdef OTA_TEST_MODE
    // Start OTA test task for bare board testing
    xTaskCreate(ota_test_task, "OTA Test", 2048, NULL, 5, NULL);
#else
    // Start menu task
    xTaskCreate(menu_task, "Menu Task", 1024, NULL, 6, NULL);
#endif

    // Start RTOS
    vTaskStartScheduler();

    while (true)
        ;
}
