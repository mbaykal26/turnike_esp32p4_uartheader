#pragma once
/*
 * gm805_uart.h – GM805 barcode scanner (UART1)
 * Reads printable ASCII until CR or LF, 115200 baud.
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialise UART1 for GM861 at 115200 baud.
 * RX=GPIO38, TX=GPIO37  (SH1.0 UART header, #6 on board)
 */
esp_err_t gm805_init(void);

/**
 * Try to read one complete barcode scan.
 * Reads UART1 until CR/LF terminator or timeout_ms expires.
 * Filters characters to printable ASCII (0x20–0x7E).
 *
 * buf     : output buffer
 * buf_size: sizeof(buf) including null terminator
 * timeout_ms: total read window in milliseconds
 *
 * Returns true and fills buf with null-terminated barcode string.
 * Returns false if nothing received or timeout expires.
 */
bool gm805_read_barcode(char *buf, size_t buf_size, uint32_t timeout_ms);
