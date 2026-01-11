/**
 * Minimal WiFi Access Point Test for Pico 2W
 * Simplified version without DHCP/DNS
 *
 * This creates WiFi network: "PicoTest"
 * Password: "password123"
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main() {
    stdio_init_all();

    printf("\n\n=== Pico 2W WiFi AP Test ===\n");
    printf("Initializing CYW43...\n");

    if (cyw43_arch_init()) {
        printf("ERROR: CYW43 init failed!\n");
        printf("This means the WiFi chip isn't responding.\n");
        printf("Possible causes:\n");
        printf("- Wrong firmware package (need pico2_w not pico_w)\n");
        printf("- Hardware defect\n");
        printf("- SDK/driver issue\n");
        while (1) {
            // Blink fast to show failure
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(100);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(100);
            printf("FAIL - CYW43 init error\n");
        }
    }

    printf("CYW43 initialized successfully!\n");
    printf("Starting Access Point...\n");

    const char *ap_name = "PicoTest";
    const char *password = "password123";

    cyw43_arch_enable_ap_mode(ap_name, password, CYW43_AUTH_WPA2_AES_PSK);

    printf("\n");
    printf("===================================\n");
    printf("SUCCESS! WiFi AP is running!\n");
    printf("SSID: %s\n", ap_name);
    printf("Password: %s\n", password);
    printf("===================================\n");
    printf("\n");
    printf("LED will blink slowly (1 sec) to show it's working.\n");
    printf("Look for WiFi network '%s' on your device.\n\n", ap_name);

    // Blink LED forever to show it's working
    bool led_on = false;
    while (1) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
        led_on = !led_on;
        printf("WiFi AP running... (LED %s)\n", led_on ? "ON" : "OFF");
        sleep_ms(1000);
    }

    return 0;
}
