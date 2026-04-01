#pragma once
/*
 * pa_access_check.h — HTTPS POST to PythonAnywhere card-access API.
 *
 * Drop-in parallel to access_check.h.  Toggle between them with
 * USE_PA_API in config.h.
 *
 * Request:  POST {"mifareId": "<uid>", "terminalId": "203"}
 * Response: {"Sonuc": true/false, "Mesaj": "...", "name": "..."}
 *
 * Same persistent-TLS design as access_check.c:
 *   pa_access_check_init()      — create client, pre-warm TLS
 *   pa_access_check()           — one card check, reuses live connection
 *   pa_access_check_keepalive() — keep connection alive every ~30 s
 *   pa_access_check_deinit()    — release resources on Ethernet loss
 */

#include "esp_err.h"
#include <stdbool.h>

#define PA_NAME_MAX   128
#define PA_MESAJ_MAX  128

typedef struct {
    bool granted;
    char name [PA_NAME_MAX];    /* full name returned by server  */
    char mesaj[PA_MESAJ_MAX];   /* server message (grant or deny)*/
} pa_access_result_t;

esp_err_t pa_access_check_init(void);
void      pa_access_check_deinit(void);
void      pa_access_check_keepalive(void);
esp_err_t pa_access_check(const char *uid_str, pa_access_result_t *result);
