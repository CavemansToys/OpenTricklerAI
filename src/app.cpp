/**
 * Minimal WiFi Access Point Test for Pico 2W
 * Based on official Raspberry Pi pico-examples
 *
 * This creates WiFi network: "PicoTest"
 * Password: "password123"
 * IP: 192.168.4.1
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "dnsserver.h"

int main() {
    stdio_init_all();

    printf("\n\n=== Pico 2W WiFi AP Test ===\n");
    printf("Initializing CYW43...\n");

    if (cyw43_arch_init()) {
        printf("ERROR: CYW43 init failed!\n");
        printf("This means the WiFi chip isn't responding.\n");
        while (1) {
            // Blink onboard LED to show failure
            printf("FAIL\n");
            sleep_ms(1000);
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
    printf("IP: 192.168.4.1\n");
    printf("===================================\n");
    printf("\n");

    // Setup IP
    ip4_addr_t gw, mask;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);

    // Start DHCP server
    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &gw, &mask);

    // Start DNS server
    dns_server_t dns_server;
    dns_server_init(&dns_server, &gw);

    printf("DHCP and DNS servers started.\n");
    printf("Blinking LED to show AP is active...\n\n");

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
