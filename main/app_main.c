/*
 * ╔═══════════════════════════════════════════════════════════════╗
 * ║  ESP32-P4 Access Control  (ESP-IDF)                          ║
 * ║  Waveshare ESP32-P4-WIFI6-POE-ETH                            ║
 * ║  PN532 NFC + GM805 Barcode + ES8311 Speaker + Telnet         ║
 * ╚═══════════════════════════════════════════════════════════════╝
 *
 * Architecture mirrors ESP32_Access_terminal_output_fixed_301025
 * but written for ESP-IDF (no Arduino libraries).
 *
 * Init order:
 *   1. GPIO: LEDs
 *   2. Audio: I2C → I2S → ES8311 codec (plays startup beep)
 *   3. GM805: UART1
 *   4. PN532: SPI2
 *   5. Ethernet: IP101 PHY + EMAC
 *   6. Telnet server: TCP port 23
 *   7. Main access-control loop
 *
 * Main loop (same logic as Arduino reference):
 *   • NFC: poll every MAIN_LOOP_DELAY_MS, 2-second duplicate window
 *   • Barcode: poll UART with 50 ms window, 2-second duplicate window
 *   • NFC health check every 10 s
 *   • Status heartbeat every 30 s
 *   • LED auto-off after 1 s
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "config.h"
#include "audio.h"
#include "pn532_spi.h"
#include "gm805_uart.h"
#include "eth_ip101.h"
#include "telnet_server.h"
#include "access_check.h"

static const char *TAG = "main";

// ─────────────────────────────────────────────────────────────────
// Helper: millisecond timestamp (wraps ~49 days — fine for uptime)
// ─────────────────────────────────────────────────────────────────

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// ─────────────────────────────────────────────────────────────────
// LED management
// ─────────────────────────────────────────────────────────────────

typedef struct {
    gpio_num_t pin;
    bool       is_on;
    uint32_t   turn_on_time;
} led_state_t;

static led_state_t s_led_green = { LED_GREEN_PIN, false, 0 };
static led_state_t s_led_red   = { LED_RED_PIN,   false, 0 };

static void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_GREEN_PIN) | (1ULL << LED_RED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(LED_GREEN_PIN, 0);
    gpio_set_level(LED_RED_PIN,   0);
}

static void led_on(led_state_t *led)
{
    gpio_set_level(led->pin, 1);
    led->is_on        = true;
    led->turn_on_time = now_ms();
}

static void led_update(void)
{
    led_state_t *leds[2] = { &s_led_green, &s_led_red };
    for (int i = 0; i < 2; i++) {
        led_state_t *l = leds[i];
        if (l->is_on && (now_ms() - l->turn_on_time >= LED_ON_TIME_MS)) {
            gpio_set_level(l->pin, 0);
            l->is_on = false;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Duplicate detection
// ─────────────────────────────────────────────────────────────────

static struct {
    uint8_t  uid[PN532_UID_MAX_LEN];
    uint8_t  uid_len;
    uint32_t last_time;
} s_last_card = { {0}, 0, 0 };

static char    s_last_barcode[BARCODE_MAX_LEN + 1] = { 0 };
static uint32_t s_last_barcode_time = 0;

static bool is_card_duplicate(const pn532_card_t *card)
{
    if (card->uid_len != s_last_card.uid_len) return false;
    if (memcmp(card->uid, s_last_card.uid, card->uid_len) != 0) return false;
    return (now_ms() - s_last_card.last_time) < CARD_READ_DELAY_MS;
}

static void update_last_card(const pn532_card_t *card)
{
    memcpy(s_last_card.uid, card->uid, card->uid_len);
    s_last_card.uid_len   = card->uid_len;
    s_last_card.last_time = now_ms();
}

static bool is_barcode_duplicate(const char *barcode)
{
    if (strcmp(barcode, s_last_barcode) != 0) return false;
    return (now_ms() - s_last_barcode_time) < CARD_READ_DELAY_MS;
}

static void update_last_barcode(const char *barcode)
{
    strlcpy(s_last_barcode, barcode, sizeof(s_last_barcode));
    s_last_barcode_time = now_ms();
}

// ─────────────────────────────────────────────────────────────────
// UID → hex string
// ─────────────────────────────────────────────────────────────────

static void uid_to_str(const uint8_t *uid, uint8_t len, char *out, size_t out_size)
{
    out[0] = '\0';
    for (uint8_t i = 0; i < len && (i * 2 + 2) < out_size; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", uid[i]);
        strncat(out, hex, out_size - strlen(out) - 1);
    }
}

// ─────────────────────────────────────────────────────────────────
// Access decision: play tone + LED + telnet log
// ─────────────────────────────────────────────────────────────────

static uint32_t s_total_granted = 0;
static uint32_t s_total_denied  = 0;
static uint32_t s_total_nfc     = 0;
static uint32_t s_total_barcode = 0;

static void handle_access(const char *uid_str, bool is_barcode)
{
    if (!eth_is_connected()) {
        telnet_logf("[SKIP] No Ethernet — cannot check %s", uid_str);
        return;
    }

    if (is_barcode) s_total_barcode++;
    else            s_total_nfc++;

    telnet_logf("[CHECK] %s: %s",
                is_barcode ? "Barcode" : "NFC", uid_str);

    access_result_t result;
    esp_err_t err = access_check(uid_str, &result);

    if (err != ESP_OK) {
        telnet_logf("[ERROR] API request failed: %s", esp_err_to_name(err));
        led_on(&s_led_red);
        audio_play_deny();
        s_total_denied++;
        return;
    }

    if (result.granted) {
        s_total_granted++;
        telnet_logf("[GRANT] User: %s %s  UID: %s",
                    result.first_name, result.last_name, uid_str);
        led_on(&s_led_green);
        audio_play_grant();
    } else {
        s_total_denied++;
        telnet_logf("[DENY]  UID: %s  (%s %s)",
                    uid_str, result.first_name, result.last_name);
        led_on(&s_led_red);
        audio_play_deny();
    }
}

// ─────────────────────────────────────────────────────────────────
// NFC health check & recovery
// ─────────────────────────────────────────────────────────────────

static bool    s_nfc_ready         = false;
static int     s_nfc_fail_count    = 0;
static uint32_t s_nfc_last_health  = 0;
static uint32_t s_nfc_last_fail_t  = 0;
static uint32_t s_nfc_last_activity= 0;

static void nfc_health_check(void)
{
    bool ok = pn532_get_firmware_version();
    if (ok) {
        s_nfc_fail_count = 0;
        s_nfc_ready      = true;
    } else {
        s_nfc_fail_count++;
        s_nfc_ready = false;
        telnet_logf("[NFC] Health check failed (fail count: %d)", s_nfc_fail_count);
    }
    s_nfc_last_health = now_ms();
}

static void nfc_try_recover(void)
{
    uint32_t retry_delay = NFC_RETRY_DELAY_MS;
    if (s_nfc_fail_count > 5) retry_delay *= 2;

    if ((now_ms() - s_nfc_last_fail_t) < retry_delay) return;

    telnet_logf("[NFC] Attempting recovery (attempt %d)...", s_nfc_fail_count + 1);

    if (s_nfc_fail_count >= 6) {
        telnet_log("[NFC] CRITICAL: PN532 not responding — check wiring and DIP switches");
    }

    // Use pn532_reconfigure() directly rather than checking FW version first.
    // pn532_reconfigure() sends a wake preamble + 1000ms settle, then SAM configure
    // + MaxRetries.  Calling pn532_get_firmware_version() first is counter-productive:
    // each failed FW version attempt leaves the PN532 SPI state machine confused,
    // causing the next FW version check to also fail — we never reach reconfigure.
    // The wake preamble inside pn532_reconfigure() resets the SPI byte counter and
    // clears the confused state before sending SAM configure.
    if (pn532_reconfigure()) {
        s_nfc_ready      = true;
        s_nfc_fail_count = 0;
        telnet_log("[NFC] Recovery successful");
    } else {
        s_nfc_fail_count++;
        s_nfc_last_fail_t = now_ms();
        telnet_logf("[NFC] Recovery failed (total fails: %d)", s_nfc_fail_count);
    }
}

// ─────────────────────────────────────────────────────────────────
// Status heartbeat
// ─────────────────────────────────────────────────────────────────

static uint32_t s_last_heartbeat = 0;
static uint32_t s_start_time     = 0;

static void print_heartbeat(void)
{
    uint32_t uptime_s  = (now_ms() - s_start_time) / 1000;
    uint32_t uptime_m  = uptime_s / 60;
    uint32_t uptime_h  = uptime_m / 60;

    char ip[16] = "N/A";
    eth_get_ip_str(ip, sizeof(ip));

    telnet_logf("──────────────────────────────────────────");
    telnet_logf("STATUS  Uptime: %02uh %02um %02us",
                (unsigned)uptime_h, (unsigned)(uptime_m % 60), (unsigned)(uptime_s % 60));
    telnet_logf("        IP: %s   ETH: %s",
                ip, eth_is_connected() ? "UP" : "DOWN");
    telnet_logf("        NFC: %s   Telnet clients: %d",
                s_nfc_ready ? "READY" : "OFFLINE", telnet_client_count());
    telnet_logf("        Reads — NFC: %lu  Barcode: %lu",
                s_total_nfc, s_total_barcode);
    telnet_logf("        Access — Granted: %lu  Denied: %lu",
                s_total_granted, s_total_denied);
    telnet_logf("──────────────────────────────────────────");

    s_last_heartbeat = now_ms();
}

// ─────────────────────────────────────────────────────────────────
// Startup banner (shared between Serial and Telnet)
// ─────────────────────────────────────────────────────────────────

static void print_banner(void)
{
    char ip[16] = "N/A";
    eth_get_ip_str(ip, sizeof(ip));
    uint8_t mac[6];
    eth_get_mac(mac);

    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32-P4 Access Control  (ESP-IDF)                   ║");
    ESP_LOGI(TAG, "║  Waveshare ESP32-P4-WIFI6-POE-ETH                     ║");
    ESP_LOGI(TAG, "║  Dual-core RISC-V @ 360MHz  /  32MB PSRAM             ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "IP:  %s", ip);
    ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "NFC: %s   Audio: ES8311 I2S Speaker",
             s_nfc_ready ? "READY" : "OFFLINE");
    ESP_LOGI(TAG, "Telnet: port %d (max %d clients)",
             TELNET_PORT, TELNET_MAX_CLIENTS);
    ESP_LOGI(TAG, "─────────────────────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────────
// app_main
// ─────────────────────────────────────────────────────────────────

void app_main(void)
{
    s_start_time = now_ms();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "ESP32-P4 Access Control starting...");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    // ── 1. LEDs ──────────────────────────────────────────────────
    ESP_LOGI(TAG, "[1/6] LEDs (GREEN=GPIO%d  RED=GPIO%d)",
             LED_GREEN_PIN, LED_RED_PIN);
    led_init();
    // Quick test flash
    gpio_set_level(LED_GREEN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_GREEN_PIN, 0);
    gpio_set_level(LED_RED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_RED_PIN, 0);

    // ── 2. Audio ─────────────────────────────────────────────────
    ESP_LOGI(TAG, "[2/6] Audio (ES8311 codec via esp_codec_dev)");
    esp_err_t ret = audio_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed: %s — continuing without speaker",
                 esp_err_to_name(ret));
    } else {
        // Startup beep: 1 kHz for 300 ms
        ESP_LOGI(TAG, "  Startup beep...");
        audio_play_grant();
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    // ── 3. GM805 barcode scanner ─────────────────────────────────
    ESP_LOGI(TAG, "[3/6] GM805 barcode scanner (UART%d RX=GPIO%d TX=GPIO%d)",
             BARCODE_UART_NUM, BARCODE_RX_PIN, BARCODE_TX_PIN);
    ret = gm805_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GM805 init failed: %s", esp_err_to_name(ret));
    }

    // ── 4. PN532 NFC ─────────────────────────────────────────────
    ESP_LOGI(TAG, "[4/6] PN532 NFC reader (SPI2 SCK=%d MISO=%d MOSI=%d CS=%d)",
             NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_CS_PIN);
    ret = pn532_init();
    if (ret == ESP_OK) {
        s_nfc_ready = true;
        ESP_LOGI(TAG, "  PN532 ready");
    } else {
        s_nfc_ready = false;
        s_nfc_fail_count = 1;
        s_nfc_last_fail_t = now_ms();
        ESP_LOGW(TAG, "  PN532 not found: %s — will retry", esp_err_to_name(ret));
    }

    // ── 5. Ethernet ──────────────────────────────────────────────
    ESP_LOGI(TAG, "[5/6] Ethernet (IP101 MDC=GPIO%d MDIO=GPIO%d RST=GPIO%d)",
             ETH_MDC_GPIO, ETH_MDIO_GPIO, ETH_PHY_RST_GPIO);
    ret = eth_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet init failed: %s", esp_err_to_name(ret));
    }

    // ── 6. Telnet server ─────────────────────────────────────────
    ESP_LOGI(TAG, "[6/6] Telnet server (port %d)", TELNET_PORT);
    bool s_telnet_started   = false;
    bool s_http_initialized = false;   // persistent HTTPS client state
    if (eth_is_connected()) {
        ret = telnet_start();
        if (ret != ESP_OK)
            ESP_LOGW(TAG, "Telnet start failed: %s", esp_err_to_name(ret));
        else
            s_telnet_started = true;

        // Pre-warm HTTPS connection while Ethernet is already up at boot.
        if (access_check_init() == ESP_OK)
            s_http_initialized = true;
    } else {
        ESP_LOGW(TAG, "  Skipped (no Ethernet) — will start when connected");
    }

    // ── Print banner ─────────────────────────────────────────────
    vTaskDelay(pdMS_TO_TICKS(200));
    print_banner();

    s_last_heartbeat = now_ms();
    s_nfc_last_health = now_ms();

    // ═══════════════════════════════════════════════════════
    // MAIN LOOP  — identical logic to Arduino reference
    // ═══════════════════════════════════════════════════════
    ESP_LOGI(TAG, "Entering main access-control loop");

    while (1) {

        // ── LED auto-off ──────────────────────────────────────
        led_update();

        // ── Heartbeat ─────────────────────────────────────────
        if ((now_ms() - s_last_heartbeat) >= STATUS_HEARTBEAT_MS) {
            print_heartbeat();
            // Send HTTP keepalive to prevent server from timing out the idle
            // persistent connection.  Runs on the same 30 s heartbeat cadence.
            if (s_http_initialized) {
                access_check_keepalive();
            }
        }

        // ── Start telnet if it wasn't started yet ─────────────
        if (!s_telnet_started && eth_is_connected()) {
            telnet_start();
            s_telnet_started = true;
        }

        // ── Skip access checks if no Ethernet ─────────────────
        if (!eth_is_connected()) {
            // If Ethernet just went down, tear down the persistent HTTP client
            // so it is recreated cleanly when the link comes back.
            if (s_http_initialized) {
                access_check_deinit();
                s_http_initialized = false;
                ESP_LOGW(TAG, "Ethernet lost — HTTPS persistent connection closed");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Init persistent HTTPS connection if not done yet ──
        // Handles the case where Ethernet was not available at boot.
        if (!s_http_initialized) {
            if (access_check_init() == ESP_OK) {
                s_http_initialized = true;
            }
        }

        // ── NFC recovery ──────────────────────────────────────
        if (!s_nfc_ready) {
            nfc_try_recover();
        }

        // ── NFC health check ──────────────────────────────────
        if (s_nfc_ready &&
            (now_ms() - s_nfc_last_health) >= NFC_HEALTH_CHECK_MS) {
            nfc_health_check();
        }

        // ── NFC watchdog — if no activity for 30 s, re-check ──
        if (s_nfc_ready &&
            (now_ms() - s_nfc_last_activity) >= NFC_WATCHDOG_TIMEOUT_MS &&
            s_nfc_last_activity > 0) {
#if DEBUG_VERBOSE_LOGGING
            ESP_LOGW(TAG, "NFC watchdog: no activity for 60 s, re-checking");
#endif
            nfc_health_check();
            s_nfc_last_activity = now_ms();
        }

        // ── Poll NFC ──────────────────────────────────────────
        if (s_nfc_ready) {
            pn532_card_t card;
            if (pn532_read_passive_target(&card)) {
                s_nfc_last_activity = now_ms();

                if (!is_card_duplicate(&card)) {
                    char uid_str[PN532_UID_MAX_LEN * 2 + 1];
                    uid_to_str(card.uid, card.uid_len, uid_str, sizeof(uid_str));
                    update_last_card(&card);
#if DEBUG_VERBOSE_LOGGING
                    ESP_LOGI(TAG, "NFC card: UID=%s  SAK=0x%02X  ATQA=0x%04X",
                             uid_str, card.sak, card.atqa);
#endif
                    handle_access(uid_str, false);
                }
                // No delay here — PN532 read already consumed ~50 ms
                continue;
            }
        }

        // ── Poll barcode (50 ms window) ───────────────────────
        char barcode[BARCODE_MAX_LEN + 1];
        if (gm805_read_barcode(barcode, sizeof(barcode), 50)) {
            if (!is_barcode_duplicate(barcode)) {
                update_last_barcode(barcode);
#if DEBUG_VERBOSE_LOGGING
                ESP_LOGI(TAG, "Barcode: %s", barcode);
#endif
                handle_access(barcode, true);
                continue;
            }
        }

        // ── Idle pause ────────────────────────────────────────
        // (NFC + barcode each spent ~50 ms, so total ~100 ms per cycle)
        // Additional small delay to yield CPU to telnet task
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
