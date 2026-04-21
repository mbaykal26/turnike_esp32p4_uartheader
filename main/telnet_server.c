/*
 * telnet_server.c – TCP telnet server for ESP32-P4 access control
 *
 * Listens on port 23, accepts up to TELNET_MAX_CLIENTS connections.
 * Sends all log lines to each client with \r\n line endings.
 * Thread-safe via FreeRTOS mutex.
 *
 * Command interface (type into the telnet session, end with Enter):
 *   ota <url>   – trigger OTA firmware update from HTTP URL
 *   reset       – reboot the device
 */

#include "telnet_server.h"
#include "config.h"
#include "ota.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>   // strdup, free
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <fcntl.h>
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "telnet";

// ─── State ───────────────────────────────────────────────────────

static int               s_client_fds[TELNET_MAX_CLIENTS];
static int               s_client_count  = 0;
static SemaphoreHandle_t s_mutex         = NULL;
static int               s_server_fd     = -1;
static bool              s_ota_running   = false;  // prevents double-trigger

// Per-client command line accumulator (reset on connect + disconnect)
static char s_cmd_buf[TELNET_MAX_CLIENTS][256];
static int  s_cmd_len[TELNET_MAX_CLIENTS];

// ─── Client slot helpers ──────────────────────────────────────────

static void init_clients(void)
{
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        s_client_fds[i] = -1;
        s_cmd_len[i]    = 0;
    }
    s_client_count = 0;
}

// Must be called while holding s_mutex.
static void remove_client(int fd)
{
    close(fd);
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (s_client_fds[i] == fd) {
            s_client_fds[i] = -1;
            s_cmd_len[i]    = 0;
            s_client_count--;
            if (s_client_count < 0) s_client_count = 0;
            ESP_LOGI(TAG, "Client disconnected (fd=%d), active=%d", fd, s_client_count);
            break;
        }
    }
}

// Must be called while holding s_mutex. Returns slot index or -1 if full.
static int add_client(int fd)
{
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (s_client_fds[i] == -1) {
            s_client_fds[i] = fd;
            s_cmd_len[i]    = 0;   // always reset — defensive against missed remove
            s_client_count++;
            return i;
        }
    }
    return -1;
}

// ─── OTA task (runs at higher priority than telnet to pre-empt it) ──

static void ota_task(void *arg)
{
    char *url = (char *)arg;
    ota_start(url);   // reboots on success; returns on failure
    free(url);
    s_ota_running = false;
    vTaskDelete(NULL);
}

// ─── Accept + read task ───────────────────────────────────────────

static void telnet_server_task(void *pvParameters)
{
    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(TELNET_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(s_server_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(s_server_fd, TELNET_MAX_CLIENTS) < 0) {
        ESP_LOGE(TAG, "listen() failed: %d", errno);
        close(s_server_fd);
        vTaskDelete(NULL);
        return;
    }

    // Non-blocking so accept() returns immediately when no new connection
    int flags = fcntl(s_server_fd, F_GETFL, 0);
    fcntl(s_server_fd, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "Telnet server listening on port %d (max %d clients)",
             TELNET_PORT, TELNET_MAX_CLIENTS);

    static const char banner[] =
        "\r\n"
        "╔══════════════════════════════════════════════╗\r\n"
        "║  ESP32-P4 Access Control Monitor             ║\r\n"
        "║  Waveshare ESP32-P4-WIFI6-POE-ETH            ║\r\n"
        "║  Dual-core RISC-V @ 360MHz                   ║\r\n"
        "╚══════════════════════════════════════════════╝\r\n"
        "Connected. Log streaming...\r\n"
        "Commands: ota <url> | reset\r\n\r\n";

    while (1) {
        // ── Accept new connection ─────────────────────────────────
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(s_server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd >= 0) {
            char client_ip[16];
            inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            int slot = add_client(client_fd);
            if (slot < 0) {
                // No slot — reject before releasing mutex so telnet_log
                // doesn't try to write to this fd first
                xSemaphoreGive(s_mutex);
                ESP_LOGW(TAG, "Max clients reached, rejecting %s", client_ip);
                const char *busy = "Server busy (max clients).\r\n";
                send(client_fd, busy, strlen(busy), 0);
                close(client_fd);
            } else {
                // Send banner under the same lock so it arrives before any
                // concurrent telnet_log() output can interleave
                send(client_fd, banner, sizeof(banner) - 1, 0);
                xSemaphoreGive(s_mutex);
                ESP_LOGI(TAG, "Telnet client #%d connected from %s, fd=%d",
                         slot, client_ip, client_fd);
            }
        }

        // ── Read client input, prune dead connections ─────────────
        char pending_ota_url[256] = {0};
        bool pending_reset        = false;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
            if (s_client_fds[i] < 0) continue;

            char rbuf[64];
            int r = recv(s_client_fds[i], rbuf, sizeof(rbuf), MSG_DONTWAIT);

            if (r == 0) {
                // Graceful close
                remove_client(s_client_fds[i]);
            } else if (r > 0) {
                // Accumulate into per-client line buffer
                for (int j = 0; j < r; j++) {
                    char c = rbuf[j];
                    if (c == '\r') continue;
                    if (c == '\n') {
                        s_cmd_buf[i][s_cmd_len[i]] = '\0';
                        // Only capture the first ota/reset command per loop tick
                        if (strncmp(s_cmd_buf[i], "ota ", 4) == 0
                                && pending_ota_url[0] == '\0') {
                            strncpy(pending_ota_url, s_cmd_buf[i] + 4,
                                    sizeof(pending_ota_url) - 1);
                        } else if (strcmp(s_cmd_buf[i], "reset") == 0) {
                            pending_reset = true;
                        }
                        s_cmd_len[i] = 0;
                    } else if (s_cmd_len[i] < (int)sizeof(s_cmd_buf[i]) - 1) {
                        s_cmd_buf[i][s_cmd_len[i]++] = c;
                    }
                }
            } else if (errno != EWOULDBLOCK && errno != EAGAIN) {
                // Socket error — treat as disconnected
                remove_client(s_client_fds[i]);
            }
            // EWOULDBLOCK/EAGAIN = no data, socket alive — nothing to do
        }
        xSemaphoreGive(s_mutex);

        // ── Dispatch OTA outside mutex (telnet_log takes mutex) ───
        if (pending_ota_url[0] != '\0') {
            if (s_ota_running) {
                telnet_log("[OTA] Already in progress — ignoring command");
            } else {
                char *url_copy = strdup(pending_ota_url);
                if (url_copy) {
                    s_ota_running = true;
                    telnet_logf("[OTA] Triggering update from: %s", url_copy);
                    // Priority 5 > telnet_srv priority 3 — OTA pre-empts logging
                    xTaskCreate(ota_task, "ota_update", 8192, url_copy, 5, NULL);
                } else {
                    telnet_log("[OTA] FAILED: out of memory");
                }
            }
        }

        // ── Dispatch reset outside mutex (telnet_log takes mutex) ─
        if (pending_reset) {
            telnet_log("[CMD] Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(200));   // let the log line flush to clients
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ─── Public API ──────────────────────────────────────────────────

esp_err_t telnet_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    init_clients();

    BaseType_t ret = xTaskCreate(telnet_server_task, "telnet_srv",
                                 4096, NULL, 3, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

void telnet_log(const char *line)
{
    if (!line) return;
    ESP_LOGI("tlog", "%s", line);

    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (s_client_fds[i] < 0) continue;
        send(s_client_fds[i], line,   strlen(line), MSG_DONTWAIT);
        send(s_client_fds[i], "\r\n", 2,            MSG_DONTWAIT);
    }
    xSemaphoreGive(s_mutex);
}

void telnet_logf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    telnet_log(buf);
}

int telnet_client_count(void)
{
    return s_client_count;
}
