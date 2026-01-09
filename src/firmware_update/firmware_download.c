#include "firmware_download.h"
#include "firmware_manager.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * HTTP URL Firmware Download Implementation
 *
 * Uses lwIP TCP client to download firmware from HTTP server
 */

// URL components
typedef struct {
    char host[128];
    uint16_t port;
    char path[128];
} parsed_url_t;

// Download context
static struct {
    bool initialized;
    firmware_download_state_t state;
    char url[256];
    parsed_url_t parsed_url;
    uint32_t expected_crc32;
    char expected_version[32];

    // TCP connection
    struct tcp_pcb *pcb;
    ip_addr_t server_ip;

    // HTTP response parsing
    bool headers_complete;
    uint32_t content_length;
    uint32_t bytes_downloaded;

    // Buffer for HTTP response
    char http_buffer[512];
    uint32_t buffer_len;

    char error_message[128];
} download_ctx;

// Helper: Set error state
static void set_download_error(const char *message) {
    download_ctx.state = DOWNLOAD_ERROR;
    strncpy(download_ctx.error_message, message, sizeof(download_ctx.error_message) - 1);
    download_ctx.error_message[sizeof(download_ctx.error_message) - 1] = '\0';
    printf("DOWNLOAD ERROR: %s\n", message);
}

// Helper: Parse URL into components
static bool parse_url(const char *url, parsed_url_t *parsed) {
    // Expected format: http://host:port/path or http://host/path

    // Skip "http://"
    const char *start = url;
    if (strncmp(start, "http://", 7) == 0) {
        start += 7;
    } else {
        return false;  // Only HTTP supported
    }

    // Find host end (: or /)
    const char *host_end = strpbrk(start, ":/");
    if (host_end == NULL) {
        // Just hostname, no port or path
        strncpy(parsed->host, start, sizeof(parsed->host) - 1);
        parsed->port = 80;
        strcpy(parsed->path, "/");
        return true;
    }

    // Copy host
    size_t host_len = host_end - start;
    if (host_len >= sizeof(parsed->host)) {
        return false;
    }
    memcpy(parsed->host, start, host_len);
    parsed->host[host_len] = '\0';

    // Parse port if present
    if (*host_end == ':') {
        parsed->port = atoi(host_end + 1);
        // Find path start
        const char *path_start = strchr(host_end, '/');
        if (path_start != NULL) {
            strncpy(parsed->path, path_start, sizeof(parsed->path) - 1);
        } else {
            strcpy(parsed->path, "/");
        }
    } else {
        // No port, default to 80
        parsed->port = 80;
        strncpy(parsed->path, host_end, sizeof(parsed->path) - 1);
    }

    parsed->path[sizeof(parsed->path) - 1] = '\0';

    printf("Parsed URL: host=%s, port=%u, path=%s\n",
           parsed->host, parsed->port, parsed->path);

    return true;
}

// TCP error callback
static void tcp_error_callback(void *arg, err_t err) {
    (void)arg;
    printf("TCP error: %d\n", err);
    set_download_error("TCP connection error");

    download_ctx.pcb = NULL;  // PCB freed by lwIP
    firmware_manager_cancel_update();
}

// TCP receive callback
static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg;

    if (err != ERR_OK || p == NULL) {
        if (p != NULL) {
            pbuf_free(p);
        }
        if (err != ERR_OK) {
            set_download_error("TCP receive error");
        }
        return ERR_OK;
    }

    // Process received data
    struct pbuf *q = p;
    while (q != NULL) {
        const char *data = (const char *)q->payload;
        uint32_t len = q->len;

        if (!download_ctx.headers_complete) {
            // Still parsing headers
            // Look for end of headers (\r\n\r\n)
            const char *header_end = strstr(data, "\r\n\r\n");
            if (header_end != NULL) {
                // Headers complete
                download_ctx.headers_complete = true;

                // Parse Content-Length
                const char *content_len_header = strstr(data, "Content-Length:");
                if (content_len_header != NULL) {
                    download_ctx.content_length = strtoul(content_len_header + 15, NULL, 10);
                    printf("Content-Length: %lu\n", download_ctx.content_length);
                }

                // Start firmware update
                const char *version = (download_ctx.expected_version[0] != '\0') ?
                                      download_ctx.expected_version : NULL;
                if (!firmware_manager_start_update(download_ctx.content_length, version)) {
                    set_download_error("Failed to start firmware update");
                    pbuf_free(p);
                    return ERR_OK;
                }

                download_ctx.state = DOWNLOAD_RECEIVING_BODY;

                // Process body data after headers
                const char *body_start = header_end + 4;
                uint32_t body_len = len - (body_start - data);
                if (body_len > 0) {
                    if (!firmware_manager_write_chunk((const uint8_t *)body_start, body_len)) {
                        set_download_error("Failed to write firmware chunk");
                        pbuf_free(p);
                        return ERR_OK;
                    }
                    download_ctx.bytes_downloaded += body_len;
                }
            }
        } else {
            // Receiving body
            if (!firmware_manager_write_chunk((const uint8_t *)data, len)) {
                set_download_error("Failed to write firmware chunk");
                pbuf_free(p);
                return ERR_OK;
            }
            download_ctx.bytes_downloaded += len;

            // Check if download complete
            if (download_ctx.bytes_downloaded >= download_ctx.content_length) {
                download_ctx.state = DOWNLOAD_VALIDATING;

                // Finalize firmware update
                if (firmware_manager_finalize_update(download_ctx.expected_crc32)) {
                    download_ctx.state = DOWNLOAD_COMPLETE;
                    printf("Download complete and validated!\n");
                } else {
                    set_download_error("Firmware validation failed");
                }
            }
        }

        q = q->next;
    }

    // Acknowledge data received
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}

// TCP connected callback
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)arg;

    if (err != ERR_OK) {
        printf("TCP connect failed: %d\n", err);
        set_download_error("Failed to connect to server");
        return err;
    }

    printf("Connected to server\n");
    download_ctx.state = DOWNLOAD_SENDING_REQUEST;

    // Build HTTP GET request
    char request[512];
    int len = snprintf(request, sizeof(request),
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Connection: close\r\n"
                      "User-Agent: OpenTrickler-OTA/1.0\r\n"
                      "\r\n",
                      download_ctx.parsed_url.path,
                      download_ctx.parsed_url.host);

    if (len < 0 || len >= (int)sizeof(request)) {
        set_download_error("HTTP request too large");
        return ERR_MEM;
    }

    printf("Sending HTTP request:\n%s", request);

    // Send HTTP request
    err_t write_err = tcp_write(tpcb, request, len, TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK) {
        printf("TCP write failed: %d\n", write_err);
        set_download_error("Failed to send HTTP request");
        return write_err;
    }

    tcp_output(tpcb);

    download_ctx.state = DOWNLOAD_RECEIVING_HEADERS;

    return ERR_OK;
}

// DNS found callback for firmware download
static void firmware_dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    (void)arg;

    if (ipaddr == NULL) {
        printf("DNS lookup failed for %s\n", download_ctx.parsed_url.host);
        set_download_error("DNS lookup failed");
        return;
    }

    printf("DNS resolved: %s -> %s\n",
           download_ctx.parsed_url.host,
           ipaddr_ntoa(ipaddr));

    download_ctx.server_ip = *ipaddr;
    download_ctx.state = DOWNLOAD_CONNECTING;

    // Create TCP PCB
    download_ctx.pcb = tcp_new();
    if (download_ctx.pcb == NULL) {
        set_download_error("Failed to create TCP connection");
        return;
    }

    // Set callbacks
    tcp_err(download_ctx.pcb, tcp_error_callback);
    tcp_recv(download_ctx.pcb, tcp_recv_callback);

    // Connect to server
    err_t err = tcp_connect(download_ctx.pcb, &download_ctx.server_ip,
                           download_ctx.parsed_url.port, tcp_connected_callback);
    if (err != ERR_OK) {
        printf("TCP connect failed: %d\n", err);
        set_download_error("Failed to initiate connection");
        tcp_close(download_ctx.pcb);
        download_ctx.pcb = NULL;
    }
}

bool firmware_download_init(void) {
    if (download_ctx.initialized) {
        return true;
    }

    memset(&download_ctx, 0, sizeof(download_ctx));
    download_ctx.state = DOWNLOAD_IDLE;
    download_ctx.initialized = true;

    printf("Firmware download module initialized\n");
    return true;
}

bool firmware_download_start(const char *url, uint32_t expected_crc32,
                              const char *expected_version) {
    if (!download_ctx.initialized) {
        return false;
    }

    if (download_ctx.state != DOWNLOAD_IDLE) {
        set_download_error("Download already in progress");
        return false;
    }

    printf("Starting firmware download from: %s\n", url);

    download_ctx.state = DOWNLOAD_PARSING_URL;

    // Save URL and parameters
    strncpy(download_ctx.url, url, sizeof(download_ctx.url) - 1);
    download_ctx.url[sizeof(download_ctx.url) - 1] = '\0';
    download_ctx.expected_crc32 = expected_crc32;

    if (expected_version != NULL) {
        strncpy(download_ctx.expected_version, expected_version,
                sizeof(download_ctx.expected_version) - 1);
        download_ctx.expected_version[sizeof(download_ctx.expected_version) - 1] = '\0';
    } else {
        download_ctx.expected_version[0] = '\0';
    }

    // Parse URL
    if (!parse_url(url, &download_ctx.parsed_url)) {
        set_download_error("Invalid URL format");
        download_ctx.state = DOWNLOAD_IDLE;
        return false;
    }

    // Start DNS lookup
    download_ctx.state = DOWNLOAD_RESOLVING_DNS;
    printf("Resolving DNS for %s...\n", download_ctx.parsed_url.host);

    ip_addr_t resolved_ip;
    err_t err = dns_gethostbyname(download_ctx.parsed_url.host, &resolved_ip,
                                  firmware_dns_callback, NULL);

    if (err == ERR_OK) {
        // IP already cached, call callback directly
        firmware_dns_callback(download_ctx.parsed_url.host, &resolved_ip, NULL);
    } else if (err != ERR_INPROGRESS) {
        set_download_error("DNS lookup failed");
        download_ctx.state = DOWNLOAD_IDLE;
        return false;
    }

    return true;
}

void firmware_download_cancel(void) {
    printf("Cancelling firmware download\n");

    if (download_ctx.pcb != NULL) {
        tcp_close(download_ctx.pcb);
        download_ctx.pcb = NULL;
    }

    firmware_manager_cancel_update();

    download_ctx.state = DOWNLOAD_IDLE;
    download_ctx.bytes_downloaded = 0;
    download_ctx.error_message[0] = '\0';
}

void firmware_download_get_status(firmware_download_status_t *status) {
    if (status == NULL) {
        return;
    }

    status->state = download_ctx.state;
    status->bytes_downloaded = download_ctx.bytes_downloaded;
    status->total_bytes = download_ctx.content_length;
    strncpy(status->url, download_ctx.url, sizeof(status->url) - 1);
    status->url[sizeof(status->url) - 1] = '\0';
    strncpy(status->error_message, download_ctx.error_message,
            sizeof(status->error_message) - 1);
    status->error_message[sizeof(status->error_message) - 1] = '\0';

    if (download_ctx.content_length > 0) {
        status->progress_percent = (download_ctx.bytes_downloaded * 100) /
                                   download_ctx.content_length;
    } else {
        status->progress_percent = 0;
    }
}

uint32_t firmware_download_get_progress(void) {
    if (download_ctx.content_length == 0) {
        return 0;
    }
    return (download_ctx.bytes_downloaded * 100) / download_ctx.content_length;
}

bool firmware_download_is_in_progress(void) {
    return (download_ctx.state != DOWNLOAD_IDLE &&
            download_ctx.state != DOWNLOAD_COMPLETE &&
            download_ctx.state != DOWNLOAD_ERROR);
}

bool firmware_download_is_complete(void) {
    return (download_ctx.state == DOWNLOAD_COMPLETE);
}

bool firmware_download_has_error(void) {
    return (download_ctx.state == DOWNLOAD_ERROR);
}
