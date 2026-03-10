/*
 * ota.c – OTA firmware update over plain HTTP
 *
 * Flow:
 *   1. Open HTTP connection to the firmware URL
 *   2. Determine content-length (if available)
 *   3. Begin writing to the inactive OTA partition
 *   4. Stream-download the .bin in 4KB chunks, writing each to flash
 *   5. Finalise, set new boot partition, reboot
 *
 * Why plain HTTP (not esp_https_ota):
 *   - Simpler for local-network firmware pushes during development
 *   - The device is on a private university LAN behind the PoE switch
 *   - Use HTTPS variant (esp_https_ota) for production if needed
 */

#include "ota.h"
#include "telnet_server.h"

#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

#define OTA_CHUNK_SIZE   4096
#define OTA_TIMEOUT_MS   30000

esp_err_t ota_start(const char *url)
{
    if (!url || url[0] == '\0') return ESP_ERR_INVALID_ARG;

    telnet_logf("[OTA] Starting update from: %s", url);
    ESP_LOGI(TAG, "OTA start: %s", url);

    // ── 1. Identify target OTA partition ──────────────────────────
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition found — is partitions.csv correct?");
        telnet_log("[OTA] FAILED: no OTA partition");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Writing to partition: %s  offset=0x%08lx  size=0x%08lx",
             update_part->label,
             (unsigned long)update_part->address,
             (unsigned long)update_part->size);

    // ── 2. Open HTTP connection ───────────────────────────────────
    esp_http_client_config_t http_cfg = {
        .url            = url,
        .timeout_ms     = OTA_TIMEOUT_MS,
        .buffer_size    = OTA_CHUNK_SIZE,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        telnet_log("[OTA] FAILED: HTTP client init");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        telnet_logf("[OTA] FAILED: cannot connect (%s)", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int http_status = esp_http_client_get_status_code(client);

    if (http_status != 200) {
        ESP_LOGE(TAG, "HTTP status %d", http_status);
        telnet_logf("[OTA] FAILED: HTTP %d", http_status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (content_len > 0)
        telnet_logf("[OTA] Firmware size: %d bytes", content_len);

    // ── 3. Begin OTA write ────────────────────────────────────────
    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        telnet_log("[OTA] FAILED: ota_begin");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    // ── 4. Stream-download and write ─────────────────────────────
    static uint8_t chunk[OTA_CHUNK_SIZE];   // static — keep off the stack
    int total_written = 0;
    int bytes_read;

    while ((bytes_read = esp_http_client_read(client, (char *)chunk, sizeof(chunk))) > 0) {
        err = esp_ota_write(ota_handle, chunk, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_write failed at offset %d: %s",
                     total_written, esp_err_to_name(err));
            telnet_logf("[OTA] FAILED: write error at %d bytes", total_written);
            esp_ota_abort(ota_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return err;
        }
        total_written += bytes_read;

        // Progress every ~100 KB
        if (content_len > 0 && (total_written % (100 * 1024)) < OTA_CHUNK_SIZE)
            telnet_logf("[OTA] Progress: %d / %d bytes (%d%%)",
                        total_written, content_len,
                        total_written * 100 / content_len);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_written == 0) {
        ESP_LOGE(TAG, "No data received");
        telnet_log("[OTA] FAILED: empty response");
        esp_ota_abort(ota_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    // ── 5. Finalise and reboot ────────────────────────────────────
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        telnet_logf("[OTA] FAILED: image validation (%s)", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        telnet_log("[OTA] FAILED: cannot set boot partition");
        return err;
    }

    telnet_logf("[OTA] SUCCESS — wrote %d bytes. Rebooting in 2 s...", total_written);
    ESP_LOGI(TAG, "OTA complete (%d bytes). Rebooting...", total_written);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;  // unreachable
}
