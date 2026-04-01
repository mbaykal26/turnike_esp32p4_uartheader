/*
 * status_reporter.c — periodic device-status POST to PythonAnywhere.
 *
 * One-shot esp_http_client per report: no persistent state, no retry.
 * Called from the 30 s heartbeat inside app_main.c so it never races
 * with access_check's persistent HTTPS connection to the university API.
 *
 * JSON payload sent:
 * {
 *   "device_name":               "Eczacılık Fakültesi 1",
 *   "nfc_online":                true,
 *   "qr_online":                 false,
 *   "uptime_seconds":            3600.0,
 *   "last_nfc_read_seconds_ago": 12.0,   // or null when never read
 *   "last_qr_read_seconds_ago":  null
 * }
 */

#include "status_reporter.h"
#include "config.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>

static const char *TAG = "reporter";

void status_reporter_send(bool  nfc_online,
                          bool  qr_online,
                          float uptime_seconds,
                          float last_nfc_read_seconds_ago,
                          float last_qr_read_seconds_ago)
{
    /* ── 1. Build JSON body ───────────────────────────────────── */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "cJSON_CreateObject failed");
        return;
    }

    cJSON_AddStringToObject(root, "device_name",    DEVICE_NAME);
    cJSON_AddBoolToObject  (root, "nfc_online",     nfc_online);
    cJSON_AddBoolToObject  (root, "qr_online",      qr_online);
    cJSON_AddNumberToObject(root, "uptime_seconds", uptime_seconds);

    if (last_nfc_read_seconds_ago >= 0.0f)
        cJSON_AddNumberToObject(root, "last_nfc_read_seconds_ago",
                                last_nfc_read_seconds_ago);
    else
        cJSON_AddNullToObject(root, "last_nfc_read_seconds_ago");

    if (last_qr_read_seconds_ago >= 0.0f)
        cJSON_AddNumberToObject(root, "last_qr_read_seconds_ago",
                                last_qr_read_seconds_ago);
    else
        cJSON_AddNullToObject(root, "last_qr_read_seconds_ago");

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        ESP_LOGE(TAG, "cJSON serialization failed");
        return;
    }

    /* ── 2. One-shot HTTP POST ───────────────────────────────── */
    esp_http_client_config_t cfg = {
        .url                         = REPORTER_URL,
        .method                      = HTTP_METHOD_POST,
        .timeout_ms                  = 5000,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        free(body);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Status sent (HTTP %d)",
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGW(TAG, "Status report failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(body);
}
