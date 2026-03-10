# ESP32-P4 Access Control — Architecture

ESP-IDF project for Waveshare ESP32-P4-WIFI6-POE-ETH.
Mirrors `ESP32_Access_terminal_output_fixed_301025` (Arduino/S3-ETH) logic,
rewritten in pure C for ESP-IDF without any Arduino libraries.

---

## Directory Structure

```
ESP32_P4_access_idf/
├── CMakeLists.txt          project-level build definition
├── sdkconfig.defaults      key Kconfig overrides (flash, TLS, ETH, lwIP)
├── CLAUDE.md               AI assistant quick-reference
├── architecture.md         this file
└── main/
    ├── CMakeLists.txt      component registration + PRIV_REQUIRES list
    ├── idf_component.yml   managed component: espressif/esp_codec_dev ≥1.3.4
    ├── config.h            ★ ALL pins / API URL / JWT / timing constants
    ├── app_main.c          entry point, init sequence, main access-control loop
    ├── audio.c / .h        ES8311 codec + NS4150B amp via esp_codec_dev
    ├── pn532_spi.c / .h    PN532 NFC hardware SPI driver (no Arduino libs)
    ├── gm805_uart.c / .h   GM805 barcode scanner UART driver
    ├── eth_ip101.c / .h    IP101 PHY + ESP32-P4 EMAC + DHCP
    ├── telnet_server.c / .h TCP port-23 log server (up to 3 clients)
    └── access_check.c / .h HTTPS POST to university API + JSON parse
```

---

## Module Responsibilities

### `config.h`
Central configuration header — **the only file you need to edit** for pin/credential changes.
Every other module `#include`s this and reads `#define` constants. Nothing is hardcoded elsewhere.

### `audio.c`
Wraps the confirmed-working speaker_test.c v4 approach:
1. `audio_init()` — opens I2C bus → starts I2S (MCLK first) → initialises ES8311 via `esp_codec_dev`
2. `audio_play_grant()` — 1000 Hz + 1500 Hz ascending with 5 ms fade-in/out
3. `audio_play_deny()` — 400 Hz with fade
Writes directly to `i2s_tx` channel handle using `i2s_channel_write()`.

### `pn532_spi.c`
Pure-C PN532 protocol implementation over ESP-IDF `spi_master`:
- Uses `SPI2_HOST`, `SPI_DEVICE_BIT_LSBFIRST` (PN532 requires LSB-first per byte)
- Each operation is **one CS-low burst** (send SPI_DIR byte + frame/dummy bytes)
  - Status poll: `[0x02, dummy]` → `rx[1]` = 0x01 when ready
  - Write: `[0x01, preamble, start, len, lcs, tfi, cmd, ..., dcs, post]`
  - ACK read: `[0x03, dummy×6]` → `rx[1..6]` = ACK pattern
  - Response read: `[0x03, dummy×35]` → `rx[1]`=PREAM, `rx[4]`=LEN, `rx[8+]`=DATA
- Public API: `pn532_init()`, `pn532_get_firmware_version()`, `pn532_read_passive_target()`

### `gm805_uart.c`
Thin UART1 wrapper:
- `gm805_init()` — installs UART driver, sets pins, 115200 8N1
- `gm805_read_barcode(buf, size, timeout_ms)` — polls `uart_read_bytes()` byte-by-byte,
  filters to printable ASCII (0x20–0x7E), breaks on CR/LF, drains leftover bytes

### `eth_ip101.c`
ESP-IDF `esp_eth` + `esp_netif` stack:
- Creates internal EMAC with MDC/MDIO on GPIO31/52 and IP101 PHY on RST=GPIO51
- Registers `ETH_EVENT` and `IP_EVENT_ETH_GOT_IP` handlers into the default event loop
- `eth_init()` blocks up to 30 s waiting for an IP address, then returns (link-optional)
- Thread-safe getters: `eth_is_connected()`, `eth_get_ip_str()`, `eth_get_mac()`

### `telnet_server.c`
FreeRTOS task (`telnet_server_task`, priority 3, 4 KB stack):
- Binds TCP socket on port 23, sets `O_NONBLOCK` on server socket
- Accept loop: adds client fd to slot array, sends welcome banner
- Prune loop: `recv(MSG_PEEK | MSG_DONTWAIT)` detects disconnects without blocking
- `telnet_log(line)` / `telnet_logf(fmt, ...)`: mutex-protected send to all active clients
  + `ESP_LOGI` to serial simultaneously

### `access_check.c`
Single function `access_check(uid_str, result)`:
1. Builds JSON body `{"mifareId":"<uid>"}`
2. Creates `esp_http_client` for HTTPS POST with Bearer header, TLS cert-verify skipped
3. Collects response body via `HTTP_EVENT_ON_DATA` callback into stack buffer (1 KB)
4. Parses JSON with `cJSON_Parse` — extracts `Sonuc` (bool), `Ad`, `Soyad` (strings)
5. Returns `ESP_OK` on any successful HTTP exchange; caller checks `result.granted`

### `app_main.c`
Init sequence and main loop:

**Init order (sequential, each step logs progress):**
```
[1/6] LED GPIO config + test flash
[2/6] audio_init()          → startup grant beep confirms speaker works
[3/6] gm805_init()
[4/6] pn532_init()          → logs FW version if found
[5/6] eth_init()            → blocks ≤30 s for DHCP
[6/6] telnet_start()        → creates server task
```

**Main loop (while(1) in app_main task):**
```
┌─────────────────────────────────────────────────────────┐
│ led_update()          auto-off after LED_ON_TIME_MS      │
│ heartbeat check       every STATUS_HEARTBEAT_MS          │
│ start telnet lazily   if ETH just came up                │
│ if !eth → sleep 100 ms, continue                         │
│ if !nfc_ready → nfc_try_recover()                        │
│ if nfc_ready  → health check every NFC_HEALTH_CHECK_MS   │
│ NFC watchdog  → re-check if silent for 30 s              │
│ pn532_read_passive_target(card, 50 ms)                   │
│   if card found + not duplicate → handle_access(uid)     │
│ gm805_read_barcode(buf, 50 ms)                           │
│   if barcode found + not duplicate → handle_access(uid)  │
│ vTaskDelay(10 ms)                                        │
└─────────────────────────────────────────────────────────┘
```

**`handle_access(uid_str, is_barcode)`:**
```
access_check(uid) → ESP_OK ?
    granted  → LED green ON + audio_play_grant() + telnet [GRANT]
    denied   → LED red ON  + audio_play_deny()  + telnet [DENY]
    API fail → LED red ON  + audio_play_deny()  + telnet [ERROR]
```

---

## Data Flow

```
Physical layer          Driver layer          Application layer
──────────────          ────────────          ─────────────────

NFC card ──────────→ pn532_spi.c ──────────→ app_main.c
                      (SPI2, 1MHz)              │
Barcode ───────────→ gm805_uart.c ────────────→ │──→ access_check.c ──→ API
                      (UART1)                    │       (HTTPS/TLS)     │
                                                 │                       │
                                                 ↓                       ↓
                                           telnet_server.c       result.granted
                                            (TCP port 23)              │
                                                                        ↓
                                                               audio.c (ES8311)
                                                               LED GPIO
```

---

## Duplicate Detection

Both NFC and barcode use the same 2-second window:
```c
CARD_READ_DELAY_MS = 2000   // if same UID seen again within 2 s → skip
```
State kept in `app_main.c` as simple structs (last UID bytes + timestamp, last barcode string + timestamp).

---

## NFC Health & Recovery

```
nfc_health_check()     every 10 s  → pn532_get_firmware_version()
                                       OK  → reset fail count
                                       FAIL → nfc_ready=false, increment fail count

nfc_try_recover()      if !nfc_ready → retry after NFC_RETRY_DELAY_MS (5 s)
                                       if fail count > 5 → delay doubles
                                       if fail count ≥ 6 → log "check wiring"

nfc_watchdog           if nfc_ready but no activity for 30 s → force health check
```

---

## Thread Safety

| Resource | Protected by |
|----------|-------------|
| Telnet client fd array | `SemaphoreHandle_t s_mutex` (mutex) |
| Ethernet IP/connected state | `volatile bool`, atomic on 32-bit RISC-V |
| Audio I2S writes | Called only from main loop task (single writer) |
| NFC SPI | Called only from main loop task (single writer) |
| GM805 UART | Called only from main loop task (single reader) |

The telnet server task only sends (no receive processing). The main task reads sensors and calls `telnet_logf()` which takes the mutex briefly. No deadlock risk.

---

## ESP-IDF Component Requirements

Declared in `main/CMakeLists.txt` under `PRIV_REQUIRES`:

| Component | Used by |
|-----------|---------|
| `esp_driver_i2s` | audio.c |
| `esp_driver_i2c` | audio.c |
| `esp_driver_spi` | pn532_spi.c |
| `esp_driver_uart` | gm805_uart.c |
| `esp_driver_gpio` | app_main.c (LEDs) |
| `esp_eth` | eth_ip101.c |
| `esp_netif` | eth_ip101.c |
| `esp_http_client` | access_check.c |
| `esp_tls` | access_check.c (HTTPS) |
| `lwip` | telnet_server.c (sockets) |
| `json` | access_check.c (cJSON) |
| `nvs_flash` | eth_ip101.c |
| `esp_timer` | app_main.c, pn532_spi.c (timestamps) |
| `espressif/esp_codec_dev` | audio.c (managed, via idf_component.yml) |
