#pragma once
#include <stdbool.h>

/*
 * status_reporter — send device status to PythonAnywhere dashboard.
 *
 * Call from the 30 s heartbeat in app_main.c.
 * Uses a simple one-shot HTTP POST (no persistent connection).
 *
 * last_nfc_read_seconds_ago / last_qr_read_seconds_ago:
 *   Pass a negative value (e.g. -1.0f) when no read has occurred yet;
 *   the field will be sent as JSON null.
 */
void status_reporter_send(bool  nfc_online,
                          bool  qr_online,
                          float uptime_seconds,
                          float last_nfc_read_seconds_ago,
                          float last_qr_read_seconds_ago);
