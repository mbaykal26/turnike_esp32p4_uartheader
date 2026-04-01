/*
 * access_check.c – HTTPS POST to Anadolu University access control API
 *
 * Switched endpoint:  dis-erisim/kart-erisim-arge  →  terminal/online-erisim
 * Old request/response format is preserved in comments below each changed block.
 *
 * Persistent connection design (unchanged):
 *   access_check_init()      — called once when Ethernet is up.
 *                              Creates the esp_http_client handle, sets fixed
 *                              headers (Content-Type, Authorization), and
 *                              pre-warms the TCP + TLS connection so the very
 *                              first real card read has zero handshake delay.
 *
 *   access_check()           — reuses the live connection (no TCP/TLS overhead).
 *                              On connection failure, closes the dead socket and
 *                              retries once with a fresh TCP + TLS connection.
 *
 *   access_check_keepalive() — send a lightweight POST every ~30 s to prevent
 *                              the server from timing out the idle connection.
 *
 *   access_check_deinit()    — release resources when Ethernet goes down.
 *
 * NEW API (terminal/online-erisim):
 *   Request:  POST {"terminalId":203,"mifareId":"<uid>","zaman":"2025-01-01T12:00:00"}
 *   Response: {"sonuc":"ERISIM_KABUL_EDILDI"|false,"isim":"Ad Soyad","mesaj":"..."}
 *   terminalId is a JSON number (not a quoted string).
 *   "sonuc" may be the string "ERISIM_KABUL_EDILDI" (granted) or boolean false.
 */

#include "access_check.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "access_check";

// ─── Persistent client state ─────────────────────────────────────

#define RESP_BUF_SIZE 1024

typedef struct {
    char   buf[RESP_BUF_SIZE];
    size_t len;
} http_resp_t;

// Module-level response buffer — its address is stable so it can safely be
// passed as user_data to the HTTP client at init time.
static http_resp_t              s_resp;
static esp_http_client_handle_t s_client = NULL;

// Authorization header is a compile-time constant — built once, used forever.
static const char s_auth_header[] = "Bearer " API_BEARER_TOKEN;

// ─── Event handler ───────────────────────────────────────────────

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (!resp) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0 &&
            resp->len + (size_t)evt->data_len < RESP_BUF_SIZE) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buf[resp->len] = '\0';
        }
        break;

    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
    default:
        break;
    }
    return ESP_OK;
}

// ─── Internal: execute one POST, retry once on connection error ──

static esp_err_t do_post(const char *body, int *http_status_out)
{
    *http_status_out = 0;

    // Reset response buffer before every request.
    memset(&s_resp, 0, sizeof(s_resp));
    esp_http_client_set_post_field(s_client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(s_client);

    if (err != ESP_OK) {
        // The server may have closed the idle connection.  Close the dead
        // socket so that the next perform() reopens TCP + TLS from scratch.
        ESP_LOGW(TAG, "HTTP request failed (%s) — reconnecting...",
                 esp_err_to_name(err));
        esp_http_client_close(s_client);

        // Reset buffer for the retry.
        memset(&s_resp, 0, sizeof(s_resp));
        err = esp_http_client_perform(s_client);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP reconnect also failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "HTTP reconnected OK");
    }

    *http_status_out = esp_http_client_get_status_code(s_client);
    return ESP_OK;
}

// ─── Init & teardown ─────────────────────────────────────────────

esp_err_t access_check_init(void)
{
    // Clean up a previous client if re-initialising after Ethernet reconnect.
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }

    esp_http_client_config_t cfg = {
        .url                         = API_URL,
        .method                      = HTTP_METHOD_POST,
        .timeout_ms                  = API_TIMEOUT_MS,
        .skip_cert_common_name_check = true,
        .transport_type              = HTTP_TRANSPORT_OVER_SSL,
        .event_handler               = http_event_handler,
        .user_data                   = &s_resp,   // stable module-level address
        .disable_auto_redirect       = true,
        .buffer_size_tx              = 2048,       // JWT Bearer header ~580 bytes
        // TCP keepalive — keeps NAT/firewall mapping alive and detects dead sockets.
        .keep_alive_enable           = true,
        .keep_alive_idle             = 10,         // seconds before first probe
        .keep_alive_interval         = 5,          // seconds between probes
        .keep_alive_count            = 3,          // probes before marking dead
    };

    s_client = esp_http_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    // Fixed headers — set once, reused for every request on this client.
    esp_http_client_set_header(s_client, "Content-Type", "application/json");
    esp_http_client_set_header(s_client, "Authorization", s_auth_header);

    // Pre-warm: establish TCP + TLS immediately so the first real card read
    // does not pay the handshake cost.  A keepalive POST is the simplest way
    // to trigger the connection — the response is ignored.
    ESP_LOGI(TAG, "HTTPS persistent client ready — pre-warming TLS connection...");
    access_check_keepalive();

    return ESP_OK;
}

void access_check_deinit(void)
{
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
}

// ─── Keepalive ping ──────────────────────────────────────────────
//
// Call every ~25–30 s from the main heartbeat.  Sending a POST prevents the
// server from closing the idle HTTP persistent connection due to its keepalive
// timeout (typically 60–120 s on nginx; set Connection: keep-alive on both
// sides).  The response is intentionally ignored — we only care that the
// TCP + TLS connection survives.

void access_check_keepalive(void)
{
    if (!s_client) return;

    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    char zaman[20];
    strftime(zaman, sizeof(zaman), "%Y-%m-%dT%H:%M:%S", &t);

    /* OLD keepalive body (dis-erisim):
     * snprintf(body, sizeof(body),
     *          "{\"mifareId\":\"\",\"terminalId\":\"%s\",\"zaman\":\"%s\"}",
     *          API_TERMINAL_ID, zaman);
     */
    /* NEW keepalive body (online-erisim): terminalId as JSON number */
    char body[128];
    snprintf(body, sizeof(body),
             "{\"terminalId\":%s,\"mifareId\":\"\",\"zaman\":\"%s\"}",
             API_TERMINAL_ID, zaman);

    // ONE attempt only — no retry.  do_post() retries which could block the
    // main loop for 2 × API_TIMEOUT_MS (up to 20 s) and stall card polling.
    // If the keepalive fails, close the dead socket here; do_post() inside
    // the next real access_check() call will re-establish TCP + TLS.
    memset(&s_resp, 0, sizeof(s_resp));
    esp_http_client_set_post_field(s_client, body, strlen(body));
    esp_err_t err = esp_http_client_perform(s_client);
    if (err != ESP_OK) {
        esp_http_client_close(s_client);   // drop dead socket; reconnect next time
        ESP_LOGW(TAG, "Keepalive failed (%s) — will reconnect at next access check",
                 esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Keepalive OK (HTTP %d, conn alive)",
                 esp_http_client_get_status_code(s_client));
    }
}

// ─── Public access check ─────────────────────────────────────────

esp_err_t access_check(const char *uid_str, access_result_t *result)
{
    if (!uid_str || !result) return ESP_ERR_INVALID_ARG;
    if (!s_client) {
        ESP_LOGE(TAG, "access_check: not initialised — Ethernet not up yet?");
        return ESP_ERR_INVALID_STATE;
    }

    result->granted  = false;
    result->name[0]  = '\0';
    result->mesaj[0] = '\0';

    // Build JSON body.
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    char zaman[20];
    strftime(zaman, sizeof(zaman), "%Y-%m-%dT%H:%M:%S", &t);

    /* OLD body (dis-erisim): terminalId as quoted string, mifareId first
     * char body[256];
     * snprintf(body, sizeof(body),
     *          "{\"mifareId\":\"%s\",\"terminalId\":\"%s\",\"zaman\":\"%s\"}",
     *          uid_str, API_TERMINAL_ID, zaman);
     */
    /* NEW body (online-erisim): terminalId as JSON number (no quotes), field order matches api_spi.py */
    char body[256];
    snprintf(body, sizeof(body),
             "{\"terminalId\":%s,\"mifareId\":\"%s\",\"zaman\":\"%s\"}",
             API_TERMINAL_ID, uid_str, zaman);

    ESP_LOGI(TAG, "POST %s  mifareId=%s", API_URL, uid_str);

    int http_status = 0;
    esp_err_t err = do_post(body, &http_status);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "HTTP status: %d  body_len: %zu", http_status, s_resp.len);
    ESP_LOGI(TAG, "Response: %s", s_resp.buf);

    if (http_status != 200 || s_resp.len == 0) {
        ESP_LOGW(TAG, "Unexpected HTTP status %d", http_status);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Parse JSON response.
    cJSON *root = cJSON_Parse(s_resp.buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %s", s_resp.buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* OLD parser (dis-erisim — Sonuc bool, Ad, Soyad):
     * cJSON *sonuc = cJSON_GetObjectItem(root, "Sonuc");
     * if (cJSON_IsBool(sonuc)) { result->granted = cJSON_IsTrue(sonuc); }
     * cJSON *ad    = cJSON_GetObjectItem(root, "Ad");
     * cJSON *soyad = cJSON_GetObjectItem(root, "Soyad");
     * if (cJSON_IsString(ad)    && ad->valuestring)
     *     strlcpy(result->first_name, ad->valuestring, ACCESS_NAME_MAX);
     * if (cJSON_IsString(soyad) && soyad->valuestring)
     *     strlcpy(result->last_name,  soyad->valuestring, ACCESS_NAME_MAX);
     */

    /* NEW parser (online-erisim — erisimSonucApiViewModel string, isim, mesaj):
     * "erisimSonucApiViewModel" == "ERISIM_KABUL_EDILDI"  → granted
     * "isim"  = full name
     * "mesaj" = status/reason message
     *
     * NOTE: will revert to dis-erisim (terminalId 203, Sonuc bool) later.
     * OLD parser (sonuc field — wrong field name for this endpoint):
     * cJSON *sonuc = cJSON_GetObjectItem(root, "sonuc");
     * if (cJSON_IsString(sonuc) && sonuc->valuestring) {
     *     result->granted = (strcmp(sonuc->valuestring, "ERISIM_KABUL_EDILDI") == 0);
     * } else if (cJSON_IsBool(sonuc)) {
     *     result->granted = cJSON_IsTrue(sonuc);
     * }
     */
    cJSON *sonuc = cJSON_GetObjectItem(root, "erisimSonucApiViewModel");
    if (cJSON_IsString(sonuc) && sonuc->valuestring) {
        result->granted = (strcmp(sonuc->valuestring, "ERISIM_KABUL_EDILDI") == 0);
    } else if (cJSON_IsBool(sonuc)) {
        result->granted = cJSON_IsTrue(sonuc);
    }

    cJSON *isim  = cJSON_GetObjectItem(root, "isim");
    cJSON *mesaj = cJSON_GetObjectItem(root, "mesaj");
    if (cJSON_IsString(isim)  && isim->valuestring)
        strlcpy(result->name,  isim->valuestring,  ACCESS_NAME_MAX);
    if (cJSON_IsString(mesaj) && mesaj->valuestring)
        strlcpy(result->mesaj, mesaj->valuestring, ACCESS_MESAJ_MAX);

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Decision: %s  Name: %s  Mesaj: %s",
             result->granted ? "GRANTED" : "DENIED",
             result->name, result->mesaj);

    return ESP_OK;
}
