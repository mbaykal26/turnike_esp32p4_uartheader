#pragma once
/*
 * telnet_server.h – TCP telnet server on port 23
 * Supports up to TELNET_MAX_CLIENTS simultaneous connections.
 * All log output goes to both UART serial and all connected telnet clients.
 */
#include <stdarg.h>
#include "esp_err.h"

/**
 * Start telnet server task (creates FreeRTOS task internally).
 * Call after Ethernet is up.
 */
esp_err_t telnet_start(void);

/**
 * Log a line to both Serial (ESP_LOGI) and all connected telnet clients.
 * Adds \r\n for telnet compatibility.
 * Thread-safe — can be called from any task.
 */
void telnet_log(const char *line);

/**
 * Printf-style variant of telnet_log.
 */
void telnet_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Returns number of currently connected telnet clients (0-TELNET_MAX_CLIENTS). */
int telnet_client_count(void);
