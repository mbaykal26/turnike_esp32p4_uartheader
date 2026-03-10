/*
 * pn532_spi.c – PN532 NFC reader driver for ESP-IDF
 *
 * PN532 SPI protocol (Application Note AN10609):
 *   - SPI Mode 0 (CPOL=0, CPHA=0), LSB first per byte
 *   - Max clock 5 MHz; we use 1 MHz for reliability
 *   - nSS active LOW, and MUST stay low for the whole read/write burst
 *
 * Transaction structure (each is ONE CS-low period):
 *   WRITE:  CS↓  [0x01, frame...]       CS↑
 *   STATUS: CS↓  [0x02, dummy]          CS↑  → rx[1] = 0x01 if ready
 *   READ:   CS↓  [0x03, dummy×N...]     CS↑  → rx[1..N] = PN532 frame
 *
 * BIT ORDER NOTE:
 *   The PN532 expects LSB-first on the wire.  SPI hardware runs MSB-first.
 *   Every byte is reversed in software before TX and after RX.
 *   Same approach as Adafruit CircuitPython PN532 library.
 *
 * CS TIMING FIX:
 *   ESP-IDF cs_ena_pretrans is IGNORED for full-duplex transactions (known
 *   driver limitation). Using GPIO-controlled CS gives us explicit setup time:
 *     CS LOW → 1 ms settle → spi_device_transmit → CS HIGH
 *   This ensures the PN532 sees CS asserted well before the first SCK edge.
 *   spics_io_num = -1  (SPI driver does NOT touch CS).
 *
 * WAKE SEQUENCE FIX:
 *   The old code sent 0x00 as the "wake byte" before the CS wake pulse, and
 *   did the CS pulse BEFORE spi_bus_initialize — meaning SCK/MOSI were still
 *   floating (GPIO input mode). PN532 received noise on its SDI during the
 *   CS-low pulse and entered an undefined state (STATUS → 0x08 forever).
 *   Fix: init SPI bus FIRST (so SCK/MOSI are proper SPI pins), THEN configure
 *   CS GPIO, THEN send PN532 wake preamble 0x55×3.
 *
 * DIAGNOSTIC MODE (s_diag_mode):
 *   Enabled during pn532_init() only.  Logs raw wire bytes at LOGI level.
 *   Expected STATUS poll: TX wire 0x40 0x00 → RX wire 0x?? 0x80 (= ready 0x01).
 *   0x00 0x00 → MISO stuck LOW (power/wiring).
 *   0xFF 0xFF → MISO floating HIGH (CS not asserting).
 *   0x10 0x10 → PN532 stuck (bad wake sequence — this fix resolves it).
 */

#include "pn532_spi.h"
#include "config.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us

static const char *TAG = "pn532";

#define SPI_DIR_WRITE   0x01
#define SPI_DIR_STATUS  0x02
#define SPI_DIR_READ    0x03

#define PN532_HOST_TO_PN532  0xD4
#define PN532_PN532_TO_HOST  0xD5

#define CMD_GET_FIRMWARE_VERSION    0x02
#define CMD_SAM_CONFIGURATION       0x14
#define CMD_IN_LIST_PASSIVE_TARGET  0x4A

#define READ_BUF_SIZE   36

static spi_device_handle_t s_spi       = NULL;
static bool                s_diag_mode = false;
static int                 s_diag_cnt  = 0;

// ─── Software bit reversal ───────────────────────────────────────

static inline uint8_t rev_byte(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

// ─── GPIO CS helpers ─────────────────────────────────────────────
// SPI driver does NOT own CS (spics_io_num = -1).
// We assert CS manually with an explicit setup delay so the PN532 sees
// CS LOW well before the first SCK edge.

static inline void cs_assert(void)
{
    gpio_set_level(NFC_CS_PIN, 0);
    esp_rom_delay_us(200);  // 200 µs setup time for reliable PN532 wake
}

static inline void cs_deassert(void)
{
    esp_rom_delay_us(50);   // brief hold after last SCK edge
    gpio_set_level(NFC_CS_PIN, 1);
}

// ─── Low-level SPI helper ────────────────────────────────────────
// Static DMA-safe buffers; single-task access only.

static uint8_t s_tx_rev[64];
static uint8_t s_rx_rev[64];

static esp_err_t spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (len == 0 || len > 64) return ESP_ERR_INVALID_SIZE;

    for (size_t i = 0; i < len; i++)
        s_tx_rev[i] = tx ? rev_byte(tx[i]) : 0x00;
    memset(s_rx_rev, 0, len);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = len * 8;
    t.tx_buffer = s_tx_rev;
    t.rx_buffer = rx ? s_rx_rev : NULL;

    /* OLD: driver-owned CS via spics_io_num = NFC_CS_PIN.
     * cs_ena_pretrans / cs_ena_posttrans were set but the ESP-IDF SPI master
     * driver silently ignores both fields for full-duplex transactions, so the
     * PN532 never got the required CS setup time before the first SCK edge.
     *
     *     esp_err_t ret = spi_device_transmit(s_spi, &t);
     */
    cs_assert();
    esp_err_t ret = spi_device_transmit(s_spi, &t);
    cs_deassert();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_device_transmit error: %s", esp_err_to_name(ret));
        return ret;
    }

    if (rx) {
        for (size_t i = 0; i < len; i++)
            rx[i] = rev_byte(s_rx_rev[i]);
    }

    // Diagnostic: log wire-level bytes during init (first 30 transactions)
    if (s_diag_mode && s_diag_cnt < 30) {
        s_diag_cnt++;
        ESP_LOGI(TAG, "  [diag#%02d] TX wire:", s_diag_cnt);
        ESP_LOG_BUFFER_HEX(TAG, s_tx_rev, len);
        if (rx) {
            ESP_LOGI(TAG, "  [diag#%02d] RX wire:", s_diag_cnt);
            ESP_LOG_BUFFER_HEX(TAG, s_rx_rev, len);
        }
    }

    return ESP_OK;
}

// ─── Build and send command frame ────────────────────────────────

static esp_err_t pn532_send_command(const uint8_t *cmd, uint8_t cmd_len)
{
    uint8_t frame[40];
    uint8_t pos = 0;

    frame[pos++] = SPI_DIR_WRITE;
    frame[pos++] = 0x00;               // Preamble
    frame[pos++] = 0x00;               // Start 1
    frame[pos++] = 0xFF;               // Start 2

    uint8_t len  = cmd_len + 1;        // +1 for TFI
    frame[pos++] = len;
    frame[pos++] = (uint8_t)(~len + 1);// LCS

    frame[pos++] = PN532_HOST_TO_PN532;// TFI
    uint8_t dcs  = PN532_HOST_TO_PN532;

    for (uint8_t i = 0; i < cmd_len; i++) {
        frame[pos++] = cmd[i];
        dcs += cmd[i];
    }
    frame[pos++] = (uint8_t)(~dcs + 1);// DCS
    frame[pos++] = 0x00;               // Postamble

    return spi_xfer(frame, NULL, pos);
}

// ─── Poll status byte ────────────────────────────────────────────

static esp_err_t pn532_wait_ready(uint32_t timeout_ms)
{
    uint8_t tx[2] = { SPI_DIR_STATUS, 0x00 };
    uint8_t rx[2];
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    // Track whether STATUS was ever NOT READY (0x00) during this call.
    // The Waveshare clone sets STATUS=0x01 correctly for the first response
    // (GetFirmwareVersion) but then leaves it STUCK at 0x01 forever after.
    // If STATUS is 0x01 from the very first poll of a new call it is almost
    // certainly a leftover signal from the previous command — accepting it
    // would cause us to read the ACK/response before the PN532 has generated
    // it, producing garbage bytes and "Bad ACK" errors.
    // Rule: only accept STATUS=0x01 as READY if we first observed 0x00 in
    // this same wait_ready call (a genuine 0x00→0x01 transition).
    bool seen_not_ready = false;
    int poll_count = 0;

    do {
        vTaskDelay(pdMS_TO_TICKS(1));  // Reduced from 2ms for faster polling
        memset(rx, 0, sizeof(rx));
        if (spi_xfer(tx, rx, 2) != ESP_OK) continue;

        poll_count++;

        if (rx[1] == 0x01 || rx[1] == 0x08) {
            // For clone chips with sticky STATUS, require at least 3 polls
            // before accepting ready state to ensure command has been processed
            if (seen_not_ready || poll_count >= 3) {
                return ESP_OK;   // genuine 0x00→0x01 transition or sufficient delay
            }
            // STATUS=0x01 was here from the very first poll — treat as sticky
            // leftover from previous command; keep polling until it clears or
            // the timeout fires.
        } else {
            seen_not_ready = true;   // saw 0x00; a subsequent 0x01 will be fresh
        }
    } while (esp_timer_get_time() < deadline);

    // Timeout: either STATUS never asserted (clone that always shows 0x00)
    // or STATUS was stuck at 0x01 (clone that never clears it after response).
    // In both cases fall through — the caller validates ACK/response bytes.
    static bool s_clone_warned = false;
    if (!s_clone_warned) {
        ESP_LOGW(TAG, "PN532 clone: STATUS=0x%02X — falling through "
                 "(sticky or never-asserted; data validated by caller)", rx[1]);
        s_clone_warned = true;
    }
    ESP_LOGD(TAG, "wait_ready: STATUS=0x%02X timeout %"PRIu32"ms — fallthrough",
             rx[1], timeout_ms);
    return ESP_OK;   // fall through; caller validates ACK/response bytes
}

// ─── Read and validate ACK ───────────────────────────────────────

static esp_err_t pn532_read_ack(void)
{
    // 20 ms: PN532 generates ACK within ~5 ms of receiving the command.
    // With sticky STATUS=0x01, seen_not_ready never fires and we fall through
    // after this timeout — 20 ms is safely past the ACK generation time.
    if (pn532_wait_ready(20) != ESP_OK) {
        ESP_LOGD(TAG, "ACK wait timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Reduced delay for better performance
    vTaskDelay(pdMS_TO_TICKS(5));  // Reduced from 10ms

    uint8_t tx[7] = { SPI_DIR_READ, 0, 0, 0, 0, 0, 0 };
    uint8_t rx[7];
    memset(rx, 0, sizeof(rx));
    if (spi_xfer(tx, rx, 7) != ESP_OK) return ESP_FAIL;

    ESP_LOGD(TAG, "ACK raw rx[1..6]: %02X %02X %02X %02X %02X %02X",
             rx[1], rx[2], rx[3], rx[4], rx[5], rx[6]);

    static const uint8_t ack[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    if (memcmp(&rx[1], ack, 6) != 0) {
        ESP_LOGW(TAG, "Bad ACK: %02X %02X %02X %02X %02X %02X",
                 rx[1], rx[2], rx[3], rx[4], rx[5], rx[6]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

// ─── Read full response frame ─────────────────────────────────────

static int pn532_read_response(uint8_t *resp_buf, uint8_t resp_max)
{
    // 100 ms: covers InListPassiveTarget scan time (MaxRetries=5 ≈ 60 ms) with margin.
    // With sticky STATUS=0x01, falls through after 100 ms; response data is
    // already in PN532 buffer by then.  Other commands (FW version, SAM,
    // MaxRetries) respond in < 5 ms — also safe to read at 100 ms fallthrough.
    if (pn532_wait_ready(100) != ESP_OK) {
        ESP_LOGD(TAG, "Response wait timeout");
        return -1;
    }

    uint8_t tx[READ_BUF_SIZE];
    uint8_t rx[READ_BUF_SIZE];
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    tx[0] = SPI_DIR_READ;

    if (spi_xfer(tx, rx, READ_BUF_SIZE) != ESP_OK) return -1;

    ESP_LOGD(TAG, "Response frame raw [0..7]: %02X %02X %02X %02X %02X %02X %02X %02X",
             rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
    if (rx[2] != 0x00 || rx[3] != 0xFF) {
        ESP_LOGD(TAG, "Bad start code: rx[2]=0x%02X rx[3]=0x%02X (expected 0x00 0xFF)",
                 rx[2], rx[3]);  // normal when no card present or scan still in progress
        return -1;
    }

    uint8_t len = rx[4];
    if (len < 2) return -1;

    uint8_t data_len = len - 2;
    if (data_len > resp_max) data_len = resp_max;

    if (8 + data_len > READ_BUF_SIZE) {
        ESP_LOGW(TAG, "Response too long (%d bytes), truncating", len);
        data_len = READ_BUF_SIZE - 8;
    }

    memcpy(resp_buf, &rx[8], data_len);
    return (int)data_len;
}

// ─── RF MaxRetries configuration ─────────────────────────────────
// RFConfiguration (0x32) ConfigItem=0x05 sets the retry counts used by
// InListPassiveTarget.  By default MxRtyPassiveActivation=0xFF (infinite),
// so the PN532 scans forever and clone chips output 0xAA (busy) on MISO
// until a card is found — we can never read a valid "no card" response.
// Setting MxRtyPassiveActivation=5 limits the scan to 6 polling cycles
// (~60 ms at 106 kbps).  After that the PN532 returns a proper response
// frame with NbTg=0 (no card found), which we can read cleanly.
// MxRtyATR and MxRtyPSL are left at their documented defaults.

static esp_err_t pn532_set_max_retries(void)
{
    // ConfigItem=0x05: MxRtyATR=0xFF  MxRtyPSL=0x01  MxRtyPassiveActivation=5
    uint8_t cmd[5] = { 0x32, 0x05, 0xFF, 0x01, 0x05 };
    esp_err_t ret = pn532_send_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;
    ret = pn532_read_ack();
    if (ret != ESP_OK) return ret;

    uint8_t resp[1] = { 0 };
    pn532_read_response(resp, sizeof(resp));   // discard — RFConfiguration has no payload
    return ESP_OK;
}

// ─── SAM configuration ───────────────────────────────────────────

static esp_err_t pn532_sam_configure(void)
{
    // For clone chips with sticky STATUS, do a dummy read to clear any stale data
    uint8_t dummy_tx[7] = { SPI_DIR_READ, 0, 0, 0, 0, 0, 0 };
    uint8_t dummy_rx[7];
    spi_xfer(dummy_tx, dummy_rx, 7);
    vTaskDelay(pdMS_TO_TICKS(2));  // Reduced from 5ms
    
    uint8_t cmd[4] = { CMD_SAM_CONFIGURATION, 0x01, 0x14, 0x01 };
    esp_err_t ret = pn532_send_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;
    
    // 20 ms: gives PN532 time to generate its ACK on marginal PoE timing.
    // (Was reduced to 10 ms by Kiro on 2026-03-04 — restored: 10 ms too tight
    // on cold PoE first-boot where the chip's ACK generation is slower.)
    vTaskDelay(pdMS_TO_TICKS(20));
    
    ret = pn532_read_ack();
    if (ret != ESP_OK) return ret;

    uint8_t resp[1] = { 0 };
    pn532_read_response(resp, sizeof(resp));
    return ESP_OK;
}

// ─── Public API ──────────────────────────────────────────────────

bool pn532_hardware_reset(void)
{
#if NFC_RST_PIN != GPIO_NUM_NC
    ESP_LOGI(TAG, "Performing hardware reset via GPIO%d", NFC_RST_PIN);
    gpio_set_level(NFC_RST_PIN, 0);  // Assert reset (active LOW)
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(NFC_RST_PIN, 1);  // Release reset
    vTaskDelay(pdMS_TO_TICKS(500));  // Wait for PN532 to boot
    return true;
#else
    ESP_LOGD(TAG, "Hardware reset not available (NFC_RST_PIN not configured)");
    return false;
#endif
}

bool pn532_reconfigure(void)
{
    // Soft-reset before SAM configure so the PN532 SPI state machine is clean
    // even after a failed prior attempt that left it confused.  Without this,
    // recovery attempts also fail with Bad ACK because the chip is stuck.
    ESP_LOGI(TAG, "pn532_reconfigure: soft-reset (wake preamble + 1000 ms settle)...");
    uint8_t wake[10] = { 0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55 };
    spi_xfer(wake, NULL, 10);
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t ret = pn532_sam_configure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pn532_reconfigure: SAM config failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Also restore MaxRetries — SAM configure resets the RF config to defaults,
    // which puts MxRtyPassiveActivation back to 0xFF (infinite scan), causing
    // the 0xAA busy flooding on InListPassiveTarget after any recovery.
    ret = pn532_set_max_retries();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "pn532_reconfigure: MaxRetries failed (continuing): %s",
                 esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "PN532 reconfigured (SAM OK, MaxRetries=5)");
    return true;
}

esp_err_t pn532_init(void)
{
    ESP_LOGI(TAG, "PN532 SPI init: SCK=%d MISO=%d MOSI=%d CS=%d  %d kHz",
             NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_CS_PIN,
             NFC_SPI_FREQ_HZ / 1000);

    /* OLD CODE (removed): Manual CS wake pulse fired BEFORE SPI bus init.
     * At that point GPIO20 (SCK) and GPIO21 (MOSI) were still in default
     * input mode — floating.  The PN532 saw CS LOW with a noisy/bouncing SCK
     * and interpreted it as SPI clock edges, corrupting its internal SPI state
     * machine.  Result: STATUS byte stuck at 0x08 on every subsequent poll.
     *
     *     gpio_reset_pin(NFC_CS_PIN);
     *     gpio_set_direction(NFC_CS_PIN, GPIO_MODE_OUTPUT);
     *     gpio_set_level(NFC_CS_PIN, 0);   // CS LOW — wake pulse
     *     vTaskDelay(pdMS_TO_TICKS(5));
     *     gpio_set_level(NFC_CS_PIN, 1);   // CS HIGH
     *     vTaskDelay(pdMS_TO_TICKS(10));   // brief settle
     */

    // ── Step 1: SPI bus — configures SCK, MOSI, MISO as SPI pins ─
    // Do this FIRST so SCK/MOSI are properly driven (SCK idle-LOW for mode 0)
    // before CS GPIO is touched.
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = NFC_MOSI_PIN,
        .miso_io_num     = NFC_MISO_PIN,
        .sclk_io_num     = NFC_SCK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 64,
    };
    esp_err_t ret = spi_bus_initialize(NFC_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* OLD dev_cfg: driver-owned CS with cs_ena timing fields.
     * Both cs_ena_pretrans and cs_ena_posttrans are silently ignored by the
     * ESP-IDF SPI master driver for full-duplex transactions — confirmed in
     * ESP-IDF issue tracker and ESP32-P4 TRM SPI chapter.  The PN532 therefore
     * saw the first SCK edge before CS had settled, causing bit-sampling errors.
     *
     *     spi_device_interface_config_t dev_cfg = {
     *         .clock_speed_hz  = NFC_SPI_FREQ_HZ,
     *         .mode            = 0,
     *         .spics_io_num    = NFC_CS_PIN,   // driver controlled CS
     *         .queue_size      = 4,
     *         .flags           = 0,
     *         .cs_ena_pretrans = 0,             // IGNORED on full-duplex
     *         .cs_ena_posttrans= 2,             // IGNORED on full-duplex
     *     };
     */

    // ── Step 2: SPI device — spics_io_num = -1 (GPIO CS) ─────────
    // CS is owned by our cs_assert/cs_deassert GPIO helpers instead.
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz  = NFC_SPI_FREQ_HZ,
        .mode            = 0,          // CPOL=0, CPHA=0
        .spics_io_num    = -1,         // GPIO-controlled CS — driver ignores CS pin
        .queue_size      = 4,
        .flags           = 0,
    };
    ret = spi_bus_add_device(NFC_SPI_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device: %s", esp_err_to_name(ret));
        return ret;
    }

    // ── Step 3: CS GPIO — configure AFTER SPI bus init ────────────
    // Now SCK is idle-LOW (SPI configured), so a CS pulse won't cause
    // phantom clocks to confuse the PN532.
    gpio_config_t cs_cfg = {
        .pin_bit_mask  = (1ULL << NFC_CS_PIN),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(NFC_CS_PIN, 1);   // CS de-asserted (HIGH)

#if NFC_RST_PIN != GPIO_NUM_NC
    // Configure optional hardware reset pin
    gpio_config_t rst_cfg = {
        .pin_bit_mask  = (1ULL << NFC_RST_PIN),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(NFC_RST_PIN, 1);  // Reset de-asserted (HIGH)
    ESP_LOGI(TAG, "  Hardware reset pin configured: GPIO%d", NFC_RST_PIN);
#endif

    // ── Step 4: Power-on stabilisation ───────────────────────────
    ESP_LOGI(TAG, "  Waiting 1000 ms for PN532 power-on / stabilisation...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // ── Step 4a: Try hardware reset if available ──────────────────
    pn532_hardware_reset();

    // ── Step 5: PN532 SPI wake preamble — 0x55 × 3 ───────────────
    // AN10609 §8.3: PN532 recognises 0x55 as the SPI synchronisation/wake
    // byte.  Sending three resets its SPI byte-framing state machine and
    // clears any undefined state from power-on or a prior failed attempt.
    /* OLD wake: single 0x00 byte — not a valid PN532 SPI wake value.
     * Combined with the premature CS pulse (above), this left the PN532 in
     * the STATUS=0x08 stuck state seen in the original log.
     *
     *     uint8_t wake[1] = { 0x00 };
     *     spi_xfer(wake, NULL, 1);
     *     vTaskDelay(pdMS_TO_TICKS(500));   // delay was here, not before wake
     */
    ESP_LOGI(TAG, "  Sending PN532 SPI wake preamble (0x55 x5)...");
    uint8_t wake[5] = { 0x55, 0x55, 0x55, 0x55, 0x55 };
    spi_xfer(wake, NULL, 5);
    vTaskDelay(pdMS_TO_TICKS(200));   // PN532 processes wake and returns to idle

    // ── Step 6: Enable diagnostics and try firmware version ───────
    s_diag_mode = true;
    s_diag_cnt  = 0;
    ESP_LOGI(TAG, "  Diagnostic mode ON — logging raw SPI wire bytes");
    ESP_LOGI(TAG, "  Expected STATUS response wire byte: 0x80=ready  0x00=not-ready  0xFF=CS not asserting");

    bool fw_ok = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (attempt > 1) {
            // A failed GetFirmwareVersion leaves the PN532 SPI state machine confused —
            // it received a command but the ACK was never cleanly consumed.  Send a new
            // wake preamble to reset the SPI byte counter before the next attempt.
            // Without this every retry also returns "Bad ACK: 00 00 00 00 00 00".
            ESP_LOGW(TAG, "  Attempt %d failed — re-waking PN532, settling %d ms...",
                     attempt - 1, attempt * 500);
            uint8_t wake_r[10] = {0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55};
            spi_xfer(wake_r, NULL, 10);
            vTaskDelay(pdMS_TO_TICKS(attempt * 500));
        }
        ESP_LOGI(TAG, "  FW version attempt %d/3...", attempt);
        if (pn532_get_firmware_version()) {
            fw_ok = true;
            break;
        }
    }
    if (!fw_ok) {
        ESP_LOGW(TAG, "  All 3 FW version attempts failed");
    }

    s_diag_mode = false;

    if (!fw_ok) {
        ESP_LOGE(TAG, "PN532 not responding after 3 attempts");
        ESP_LOGE(TAG, "Check: 1) Wiring SCK=GPIO20 MOSI=GPIO21 MISO=GPIO22 CS=GPIO23");
        ESP_LOGE(TAG, "       2) DIP switches SW1=ON SW2=OFF (SPI mode)");
        ESP_LOGE(TAG, "       3) VCC: 3.3V or 5V with regulator jumper correct");
        ESP_LOGE(TAG, "       4) STATUS wire byte above: 0x00=MISO low, 0xFF=CS open, 0x10=wake failed");
        return ESP_ERR_NOT_FOUND;
    }

    // Give PN532 extra time to fully initialise its internal analog / RF section
    // after firmware version is confirmed.  On cold PoE boot the chip responds
    // to GetFirmwareVersion within ~300 ms of wake, but SAM configure requires
    // a fully-settled state.  Without this delay the first-boot Bad ACK occurs.
    ESP_LOGI(TAG, "  PN532 FW confirmed — waiting 500 ms for full internal stabilisation...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Retry SAM configure up to 3 times.  If the first attempt fails (Bad ACK
    // on cold PoE start), send an extended wake preamble and wait 2 s before
    // the next attempt to let the chip recover from the bad state.
    esp_err_t sam_ret = ESP_ERR_INVALID_RESPONSE;
    for (int sam_attempt = 1; sam_attempt <= 3 && sam_ret != ESP_OK; sam_attempt++) {
        if (sam_attempt > 1) {
            ESP_LOGW(TAG, "  SAM attempt %d/3: soft-reset (wake preamble + 2000 ms)...",
                     sam_attempt);
            uint8_t wake2[10] = { 0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55 };
            spi_xfer(wake2, NULL, 10);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        s_diag_mode = true;
        s_diag_cnt  = 0;
        ESP_LOGI(TAG, "  Attempting SAM configuration (attempt %d/3)...", sam_attempt);
        sam_ret = pn532_sam_configure();
        s_diag_mode = false;
    }

    if (sam_ret != ESP_OK) {
        ESP_LOGE(TAG, "SAM configuration failed after 3 attempts: %s", esp_err_to_name(sam_ret));
        return sam_ret;
    }

    ret = pn532_set_max_retries();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MaxRetries config failed (continuing): %s", esp_err_to_name(ret));
        // Non-fatal: NFC will still work but may log "Bad start code" when idle
    }
    ESP_LOGI(TAG, "PN532 SAM configured, MaxRetries=5 — ready to read cards");

    return ESP_OK;
}

bool pn532_get_firmware_version(void)
{
    uint8_t cmd = CMD_GET_FIRMWARE_VERSION;
    if (pn532_send_command(&cmd, 1) != ESP_OK) return false;
    if (pn532_read_ack() != ESP_OK)             return false;

    uint8_t resp[4] = { 0 };
    int n = pn532_read_response(resp, sizeof(resp));
    if (n < 4) return false;

    ESP_LOGI(TAG, "PN532: FW=%d.%d  IC=0x%02X  Support=0x%02X",
             resp[1], resp[2], resp[0], resp[3]);
    return (resp[0] == 0x32);
}

bool pn532_read_passive_target(pn532_card_t *card)
{
    if (!card) return false;

    uint8_t cmd[3] = { CMD_IN_LIST_PASSIVE_TARGET, 0x01, 0x00 };
    if (pn532_send_command(cmd, sizeof(cmd)) != ESP_OK) return false;
    if (pn532_read_ack() != ESP_OK)                     return false;

    // Note: pn532_wait_ready() always returns ESP_OK (falls through on timeout,
    // never returns an error).  An extra pn532_wait_ready(timeout_ms) call here
    // was dead code that only burned timeout_ms milliseconds.  Removed.
    // pn532_read_response() has its own internal wait_ready(75 ms) which covers
    // the full InListPassiveTarget scan time (MaxRetries=5 ≈ 60 ms).

    uint8_t resp[32] = { 0 };
    int n = pn532_read_response(resp, sizeof(resp));
    if (n < 1) return false;

    if (resp[0] == 0) return false;

    if (n < 6) return false;

    card->atqa    = ((uint16_t)resp[2] << 8) | resp[3];
    card->sak     = resp[4];
    card->uid_len = resp[5];
    if (card->uid_len > PN532_UID_MAX_LEN) card->uid_len = PN532_UID_MAX_LEN;
    memcpy(card->uid, &resp[6], card->uid_len);

    return true;
}
