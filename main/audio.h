#pragma once
/*
 * audio.h – ES8311 + NS4150B speaker driver
 * Uses espressif/esp_codec_dev component (same as speaker_test.c v4 CONFIRMED WORKING)
 */
#include "esp_err.h"

/**
 * Initialise I2C, I2S, and ES8311 codec.
 * Must be called once from app_main before any play_* call.
 * Returns ESP_OK on success.
 */
esp_err_t audio_init(void);

/**
 * Play access-granted sound: 1000 Hz (200 ms) + 100 ms silence + 1500 Hz (200 ms).
 * Blocking – returns when audio finishes.
 */
void audio_play_grant(void);

/**
 * Play access-denied sound: 400 Hz (400 ms).
 * Blocking – returns when audio finishes.
 */
void audio_play_deny(void);
