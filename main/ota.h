#pragma once
/*
 * ota.h – OTA firmware update over HTTP (pull from URL)
 *
 * Usage:
 *   ota_start("http://192.168.1.100:8080/firmware.bin");
 *
 * The device downloads the firmware in chunks, writes it to the
 * inactive OTA partition, then reboots into the new firmware.
 *
 * To serve firmware from a laptop:
 *   cd build/
 *   python -m http.server 8080
 * Then trigger: ota_start("http://<laptop-ip>:8080/esp32_p4_access.bin")
 */
#include "esp_err.h"

/**
 * Download firmware from url and update over-the-air.
 * Blocks until complete (or failed). Reboots on success.
 * Returns ESP_OK only if something unexpected prevents the reboot call,
 * otherwise the device restarts before returning.
 */
esp_err_t ota_start(const char *url);
