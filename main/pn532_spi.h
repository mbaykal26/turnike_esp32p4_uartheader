#pragma once
/*
 * pn532_spi.h – PN532 NFC reader driver for ESP-IDF
 *
 * Bit order: software LSB-first reversal on standard MSB-first SPI bus.
 * (SPI_DEVICE_BIT_LSBFIRST hardware flag is unreliable on ESP32-P4.)
 *
 * Pins (same as Arduino project):
 *   SCK  = GPIO20
 *   MISO = GPIO21
 *   MOSI = GPIO22
 *   CS   = GPIO23
 *
 * PN532 board DIP switches: SW1=ON, SW2=OFF  (selects SPI mode)
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Maximum UID length for ISO14443A cards
#define PN532_UID_MAX_LEN  7

typedef struct {
    uint8_t  uid[PN532_UID_MAX_LEN];
    uint8_t  uid_len;
    uint16_t atqa;   // Answer To Request type A
    uint8_t  sak;    // Select Acknowledge
} pn532_card_t;

/**
 * Initialise SPI bus and PN532.
 * Sends GetFirmwareVersion to verify chip is alive.
 * Returns ESP_OK on success.
 */
esp_err_t pn532_init(void);

/**
 * Read firmware version registers.
 * Returns true if valid PN532 response received.
 */
bool pn532_get_firmware_version(void);

/**
 * Re-send SAM configuration (Normal mode) without re-initialising SPI bus.
 * Call after a successful pn532_get_firmware_version() in recovery paths.
 * Returns true if SAM configuration was accepted by the PN532.
 */
bool pn532_reconfigure(void);

/**
 * Perform hardware reset of PN532 via RSTPD_N pin (if connected).
 * Returns true if reset pin is configured and reset was performed.
 */
bool pn532_hardware_reset(void);

/**
 * Poll for an ISO14443A card (NFC tag or MIFARE card).
 * Returns true and fills *card if a card is found.
 * Returns false if no card present or error.
 * (Scan duration is controlled internally by MaxRetries=5 ≈ 60 ms.)
 */
bool pn532_read_passive_target(pn532_card_t *card);
