/*
 * audio.c – ES8311 + NS4150B speaker via esp_codec_dev
 *
 * Directly derived from speaker_test.c v4 (CONFIRMED WORKING 2026-03-01).
 * Key facts:
 *   DOUT = GPIO9  → ES8311 DSDIN  (DAC input)   ← PIN FIX
 *   DIN  = GPIO11 ← ES8311 ASDOUT (ADC output)
 *   MCLK = 256 × 16000 = 4,096,000 Hz
 *   channel_mask = 0x03 (NOT the undefined macro version)
 */

#include "audio.h"
#include "config.h"
#include "tts_data.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"

static const char *TAG = "audio";

static i2s_chan_handle_t       s_i2s_tx    = NULL;
static i2s_chan_handle_t       s_i2s_rx    = NULL;
static esp_codec_dev_handle_t  s_codec     = NULL;
static i2c_master_bus_handle_t s_i2c_bus   = NULL;

// Background TTS task state
static volatile bool s_tts_abort = false;
static TaskHandle_t  s_tts_task  = NULL;

#define BUF_FRAMES 256

// ─── I2C bus init ────────────────────────────────────────────────

static esp_err_t init_i2c(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = I2C_NUM_0,
        .sda_io_num             = I2C_SDA_PIN,
        .scl_io_num             = I2C_SCL_PIN,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Quick chip-ID probe to confirm ES8311 is reachable
    i2c_device_config_t probe_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8311_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    i2c_master_dev_handle_t probe = NULL;
    if (i2c_master_bus_add_device(s_i2c_bus, &probe_cfg, &probe) == ESP_OK) {
        uint8_t r, id1 = 0xFF, id2 = 0xFF;
        r = 0xFD; i2c_master_transmit_receive(probe, &r, 1, &id1, 1, 500);
        r = 0xFE; i2c_master_transmit_receive(probe, &r, 1, &id2, 1, 500);
        ESP_LOGI(TAG, "ES8311 chip ID: 0x%02X 0x%02X  %s",
                 id1, id2,
                 (id1 == 0x83 && id2 == 0x11) ? "DETECTED ✓" : "NOT FOUND ✗");
        i2c_master_bus_rm_device(probe);
    }
    return ESP_OK;
}

// ─── I2S init — starts MCLK (must run BEFORE codec init) ────────

static esp_err_t init_i2s(void)
{
    ESP_LOGI(TAG, "I2S: MCLK=%d BCLK=%d WS=%d DOUT=%d(→DAC) DIN=%d(←ADC)",
             I2S_MCLK_PIN, I2S_BCLK_PIN, I2S_LRCK_PIN,
             I2S_DOUT_PIN, I2S_DIN_PIN);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);
    if (ret != ESP_OK) return ret;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_LRCK_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_DIN_PIN,
            .invert_flags = { .mclk_inv = false,
                              .bclk_inv = false,
                              .ws_inv   = false },
        },
    };
    // MCLK = 256 × 16000 = 4,096,000 Hz (matches ES8311 coefficient table)
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ret = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (ret != ESP_OK) return ret;
    ret = i2s_channel_init_std_mode(s_i2s_rx, &std_cfg);
    if (ret != ESP_OK) return ret;

    ret = i2s_channel_enable(s_i2s_tx);
    if (ret != ESP_OK) return ret;
    ret = i2s_channel_enable(s_i2s_rx);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "I2S running, MCLK = %d Hz", 256 * AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

// ─── ES8311 codec init via esp_codec_dev component ──────────────

static esp_err_t init_codec(void)
{
    ESP_LOGI(TAG, "ES8311: init via esp_codec_dev...");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = I2C_NUM_0,
        .addr       = ES8311_CODEC_DEFAULT_ADDR,   // 0x18
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) { ESP_LOGE(TAG, "I2C ctrl if failed"); return ESP_FAIL; }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .tx_handle = s_i2s_tx,
        .rx_handle = s_i2s_rx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) { ESP_LOGE(TAG, "I2S data if failed"); return ESP_FAIL; }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) { ESP_LOGE(TAG, "GPIO if failed"); return ESP_FAIL; }

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if      = ctrl_if,
        .gpio_if      = gpio_if,
        .codec_mode   = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode  = false,
        .use_mclk     = true,
        .pa_pin       = PA_CTRL_PIN,
        .pa_reverted  = false,    // HIGH = amp ON
        .hw_gain = {
            .pa_voltage        = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) { ESP_LOGE(TAG, "ES8311 codec if failed"); return ESP_FAIL; }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) { ESP_LOGE(TAG, "codec dev create failed"); return ESP_FAIL; }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0x03,   // L + R
        .sample_rate     = AUDIO_SAMPLE_RATE,
    };
    esp_err_t ret = esp_codec_dev_open(s_codec, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "codec open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_codec_dev_set_out_vol(s_codec, 55);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "vol set warning: %s (continuing)", esp_err_to_name(ret));

    ESP_LOGI(TAG, "ES8311 ready, vol=50  PA=GPIO%d", PA_CTRL_PIN);
    return ESP_OK;
}

// ─── Public API ──────────────────────────────────────────────────

esp_err_t audio_init(void)
{
    esp_err_t ret;

    ret = init_i2c();
    if (ret != ESP_OK) return ret;

    ret = init_i2s();
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(50));   // wait for MCLK to stabilise

    ret = init_codec();
    return ret;
}

// Internal: write sine wave to I2S TX with fade in/out
static void play_tone(int freq_hz, int duration_ms)
{
    if (freq_hz <= 0 || duration_ms <= 0) return;

    int     total = AUDIO_SAMPLE_RATE * duration_ms / 1000;
    int16_t buf[BUF_FRAMES * 2];   // stereo interleaved
    size_t  written = 0;
    int     gen = 0;

    while (gen < total) {
        int n = total - gen;
        if (n > BUF_FRAMES) n = BUF_FRAMES;

        for (int i = 0; i < n; i++) {
            int   idx   = gen + i;
            float phase = 2.0f * (float)M_PI * freq_hz * idx
                          / (float)AUDIO_SAMPLE_RATE;
            float amp   = (float)TONE_AMPLITUDE;

            if      (idx < TONE_FADE_SAMPLES)
                amp *= (float)idx / TONE_FADE_SAMPLES;
            else if (idx >= total - TONE_FADE_SAMPLES)
                amp *= (float)(total - idx) / TONE_FADE_SAMPLES;

            int16_t s = (int16_t)(amp * sinf(phase));
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }

        i2s_channel_write(s_i2s_tx, buf,
                          n * 2 * sizeof(int16_t),
                          &written, pdMS_TO_TICKS(1000));
        gen += n;
    }
}

static void play_silence(int ms)
{
    if (ms <= 0) return;
    int     total = AUDIO_SAMPLE_RATE * ms / 1000;
    int16_t buf[BUF_FRAMES * 2];
    memset(buf, 0, sizeof(buf));
    size_t  written;
    int     gen = 0;

    while (gen < total) {
        int n = total - gen;
        if (n > BUF_FRAMES) n = BUF_FRAMES;
        i2s_channel_write(s_i2s_tx, buf, n * 4, &written, pdMS_TO_TICKS(1000));
        gen += n;
    }
}

static void play_pcm_mono(const int16_t *samples, int count);   // forward decl

// ─── Background TTS task ─────────────────────────────────────────

#define TTS_TYPE_GRANT  0
#define TTS_TYPE_DENY   1

static void tts_bg_task(void *arg)
{
    int type = (int)(intptr_t)arg;
    if (type == TTS_TYPE_GRANT)
        play_pcm_mono(tts_grant_samples, TTS_GRANT_SAMPLES_COUNT);
    else
        play_pcm_mono(tts_deny_samples, TTS_DENY_SAMPLES_COUNT);
    s_tts_task = NULL;   // signal: task finished (checked by abort_tts wait loop)
    vTaskDelete(NULL);
}

// Stop any running TTS before playing a tone — waits at most ~50 ms
// (one chunk = 256/16000 Hz = 16 ms; 5 × 10 ms polls covers 2 chunks).
static void abort_tts(void)
{
    if (s_tts_task == NULL) return;
    s_tts_abort = true;
    for (int i = 0; i < 5 && s_tts_task != NULL; i++)
        vTaskDelay(pdMS_TO_TICKS(10));
    s_tts_abort = false;
}

void audio_play_grant(void)
{
    abort_tts();   // stop any ongoing TTS before playing tone
    play_tone(TONE_GRANT_1_HZ, TONE_GRANT_DUR_MS);
    play_silence(TONE_GRANT_GAP_MS);
    play_tone(TONE_GRANT_2_HZ, TONE_GRANT_DUR_MS);
    play_silence(30);
}

void audio_play_deny(void)
{
    abort_tts();   // stop any ongoing TTS before playing tone
    play_tone(TONE_DENY_HZ, TONE_DENY_DUR_MS);
    play_silence(30);
}

// ─── PCM playback (for TTS phrases) ──────────────────────────────
//
// Plays mono 16-bit PCM samples from a C array generated by tools/gen_tts.py.
// Duplicates each sample to both L and R channels (stereo interleaved).
// PA amp stays on — managed by esp_codec_dev, no GPIO needed here.
// Uses same 256-frame chunk pattern as play_tone().

static void play_pcm_mono(const int16_t *samples, int count)
{
    if (!samples || count <= 0) return;

    int16_t buf[BUF_FRAMES * 2];   // stereo interleaved
    size_t  written;
    int     offset = 0;

    while (offset < count) {
        if (s_tts_abort) goto done;   // abort requested — exit cleanly without tail silence

        int n = count - offset;
        if (n > BUF_FRAMES) n = BUF_FRAMES;

        for (int i = 0; i < n; i++) {
            buf[i * 2]     = samples[offset + i];  // L
            buf[i * 2 + 1] = samples[offset + i];  // R
        }

        i2s_channel_write(s_i2s_tx, buf, n * 2 * sizeof(int16_t),
                          &written, pdMS_TO_TICKS(1000));
        offset += n;
    }

    play_silence(50);   // tail silence — prevents click at end of phrase

done:;
}

void audio_play_tts_grant(void)
{
    // OLD (blocking): play_pcm_mono(tts_grant_samples, TTS_GRANT_SAMPLES_COUNT);
    s_tts_abort = false;
    xTaskCreate(tts_bg_task, "tts_bg", 4096,
                (void *)(intptr_t)TTS_TYPE_GRANT, 2, &s_tts_task);
}

void audio_play_tts_deny(void)
{
    // OLD (blocking): play_pcm_mono(tts_deny_samples, TTS_DENY_SAMPLES_COUNT);
    s_tts_abort = false;
    xTaskCreate(tts_bg_task, "tts_bg", 4096,
                (void *)(intptr_t)TTS_TYPE_DENY, 2, &s_tts_task);
}
