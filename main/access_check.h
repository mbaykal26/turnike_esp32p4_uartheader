#pragma once
/*
 * access_check.h – HTTPS POST to Anadolu University access control API
 *
 * Persistent connection: call access_check_init() once when Ethernet is up,
 * then access_check() reuses the live TLS connection with no handshake overhead.
 * Call access_check_keepalive() every ~30 s to prevent the server from timing
 * out the idle connection.  Call access_check_deinit() if Ethernet goes down.
 *
 * API:
 *   POST {"mifareId": "<uid_or_barcode>", "terminalId": "...", "zaman": "..."}
 *   Response: { "Sonuc": true/false, "Ad": "FirstName", "Soyad": "LastName" }
 */
#include <stdbool.h>
#include "esp_err.h"

#define ACCESS_NAME_MAX  64

typedef struct {
    bool    granted;
    char    first_name[ACCESS_NAME_MAX];
    char    last_name[ACCESS_NAME_MAX];
} access_result_t;

/**
 * Create the persistent HTTPS client and pre-warm the TCP + TLS connection.
 * Must be called once after Ethernet has a valid IP address.
 * Safe to call again after Ethernet reconnects (re-initialises the client).
 */
esp_err_t access_check_init(void);

/**
 * Release the persistent client.  Call when Ethernet goes down.
 */
void access_check_deinit(void);

/**
 * Send a lightweight POST to keep the server connection alive.
 * Call every ~25–30 s from the status heartbeat.
 * No-op if the client is not initialised.
 */
void access_check_keepalive(void);

/**
 * Perform HTTPS POST to the API with the given UID/barcode string.
 * Reuses the persistent connection — no TCP or TLS handshake overhead.
 * On connection drop, reconnects transparently (one-time handshake cost).
 * Fills *result with grant decision and user name on success.
 * Returns ESP_OK on successful HTTP transaction (regardless of grant/deny).
 * Returns error code if network/TLS fails after reconnect attempt.
 */
esp_err_t access_check(const char *uid_str, access_result_t *result);
