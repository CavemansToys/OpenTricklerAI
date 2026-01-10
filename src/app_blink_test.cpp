/*
 * MINIMAL LED BLINK TEST for Pico 2W (RP2350)
 * Tests: FreeRTOS + LED control only
 */

#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

// Blink task - just blink the LED forever
void blink_task(void *pvParameters) {
    (void)pvParameters;

    // Initialize WiFi chip (needed for LED control on Pico W)
    if (cyw43_arch_init()) {
        printf("ERROR: Failed to initialize cyw43\n");
        while (1) {
            tight_loop_contents();
        }
    }

    printf("cyw43 initialized - starting blink\n");

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);  // LED on
        vTaskDelay(pdMS_TO_TICKS(250));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);  // LED off
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB serial

    printf("\n");
    printf("========================================\n");
    printf("MINIMAL BLINK TEST - Pico 2W (RP2350)\n");
    printf("========================================\n");

    // Create blink task
    xTaskCreate(blink_task, "Blink", 512, NULL, 1, NULL);

    printf("Starting FreeRTOS scheduler...\n");
    vTaskStartScheduler();

    // Should never reach here
    while (1) {
        tight_loop_contents();
    }
}
