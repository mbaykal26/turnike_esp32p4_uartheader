/*
 * pa_access_check.c — HTTPS POST to PythonAnywhere card-access API.
 *
 * Persistent-TLS design (mirrors access_check.c):
 *   pa_access_check_init()      — create client, pre-warm TCP + TLS
 *   pa_access_check()           — card check, reuses live connection;
 *                                  reconnects transparently on drop
 *   pa_access_check_keepalive() — lightweight POST every ~30 s to
 *                                  prevent server closing idle connection
 *   pa_access_check_deinit()    — release resources when Ethernet drops
 *
 * Request:
 *   POST PA_ACCESS_URL
 *   Authorization: Bearer PA_ACCESS_TOKEN
 *   Content-Type: application/json
 *   {"mifareId":"<uid>","terminalId":"<API_TERMINAL_ID>"}
 *
 * Response:
 *   {"Sonuc": true/false, "Mesaj": "...", "name": "..."}
 */

#include "pa_access_check.h"
#include "config.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "pa_access";

// ─── Persistent client state ────────────────────────────────────

#define RESP_BUF_SIZE 1024

typedef struct {
    char   buf[RESP_BUF_SIZE];
    size_t len;
} http_resp_t;

static http_resp_t              s_resp;
static esp_http_client_handle_t s_client = NULL;

static const char s_auth_header[] = "Bearer " PA_ACCESS_TOKEN;

// ─── Event handler ───────────────────────────────────────────────

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (!resp) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (evt->data_len > 0 &&
            resp->len + (size_t)evt->data_len < RESP_BUF_SIZE) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buf[resp->len] = '\0';
        }
    }
    return ESP_OK;
}

// ─── Internal: execute one POST, retry once on connection error ──

static esp_err_t do_post(const char *body, int *http_status_out)
{
    *http_status_out = 0;

    memset(&s_resp, 0, sizeof(s_resp));
    esp_http_client_set_post_field(s_client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(s_client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Request failed (%s) — reconnecting...",
                 esp_err_to_name(err));
        esp_http_client_close(s_client);

        memset(&s_resp, 0, sizeof(s_resp));
        err = esp_http_client_perform(s_client);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reconnect also failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Reconnected OK");
    }

    *http_status_out = esp_http_client_get_status_code(s_client);
    return ESP_OK;
}

// ─── Init & teardown ─────────────────────────────────────────────

esp_err_t pa_access_check_init(void)
{
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }

    esp_http_client_config_t cfg = {
        .url                         = PA_ACCESS_URL,
        .method                      = HTTP_METHOD_POST,
        .timeout_ms                  = API_TIMEOUT_MS,
        .skip_cert_common_name_check = true,
        .transport_type              = HTTP_TRANSPORT_OVER_SSL,
        .event_handler               = http_event_handler,
        .user_data                   = &s_resp,
        .disable_auto_redirect       = true,
        .keep_alive_enable           = true,
        .keep_alive_idle             = 10,
        .keep_alive_interval         = 5,
        .keep_alive_count            = 3,
    };

    s_client = esp_http_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_header(s_client, "Content-Type",  "application/json");
    esp_http_client_set_header(s_client, "Authorization", s_auth_header);

    ESP_LOGI(TAG, "PA persistent client ready — pre-warming TLS...");
    pa_access_check_keepalive();

    return ESP_OK;
}

void pa_access_check_deinit(void)
{
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
}

// ─── Keepalive ping ──────────────────────────────────────────────

void pa_access_check_keepalive(void)
{
    if (!s_client) return;

    char body[64];
    snprintf(body, sizeof(body),
             "{\"mifareId\":\"\",\"terminalId\":\"%s\"}", API_TERMINAL_ID);

    memset(&s_resp, 0, sizeof(s_resp));
    esp_http_client_set_post_field(s_client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(s_client);
    if (err != ESP_OK) {
        esp_http_client_close(s_client);
        ESP_LOGW(TAG, "Keepalive failed (%s) — will reconnect at next check",
                 esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Keepalive OK (HTTP %d)",
                 esp_http_client_get_status_code(s_client));
    }
}

// ─── Public card check ───────────────────────────────────────────

esp_err_t pa_access_check(const char *uid_str, pa_access_result_t *result)
{
    if (!uid_str || !result) return ESP_ERR_INVALID_ARG;
    if (!s_client) {
        ESP_LOGE(TAG, "Not initialised — Ethernet not up yet?");
        return ESP_ERR_INVALID_STATE;
    }

    result->granted  = false;
    result->name[0]  = '\0';
    result->mesaj[0] = '\0';

    /* Build request body — include_photo:false keeps response small (~100 bytes) */
    char body[160];
    snprintf(body, sizeof(body),
             "{\"mifareId\":\"%s\",\"terminalId\":\"%s\",\"include_photo\":false}",
             uid_str, API_TERMINAL_ID);

    ESP_LOGI(TAG, "POST %s  mifareId=%s", PA_ACCESS_URL, uid_str);

    int http_status = 0;
    esp_err_t err = do_post(body, &http_status);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "HTTP %d  body_len: %zu", http_status, s_resp.len);
    ESP_LOGD(TAG, "Response: %s", s_resp.buf);

    if (http_status != 200 || s_resp.len == 0) {
        ESP_LOGW(TAG, "Unexpected HTTP status %d", http_status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Parse JSON response */
    cJSON *root = cJSON_Parse(s_resp.buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %s", s_resp.buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *sonuc = cJSON_GetObjectItem(root, "Sonuc");
    cJSON *mesaj = cJSON_GetObjectItem(root, "Mesaj");
    cJSON *name  = cJSON_GetObjectItem(root, "name");

    if (cJSON_IsBool(sonuc))
        result->granted = cJSON_IsTrue(sonuc);

    if (cJSON_IsString(mesaj) && mesaj->valuestring)
        strlcpy(result->mesaj, mesaj->valuestring, PA_MESAJ_MAX);

    if (cJSON_IsString(name) && name->valuestring)
        strlcpy(result->name, name->valuestring, PA_NAME_MAX);

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Decision: %s  Name: %s  Mesaj: %s",
             result->granted ? "GRANTED" : "DENIED",
             result->name, result->mesaj);

    return ESP_OK;
}
