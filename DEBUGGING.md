# ESP32-P4 Access Control — Debugging Reference

> Board: Waveshare ESP32-P4-WIFI6-POE-ETH
> Working dir: `C:\Users\Murat\Desktop\ESP32_P4_access_idf`

---

## Quick-Start Commands

```bash
# Build only (check for compile errors)
idf.py build

# Flash + open monitor
idf.py -p COM5 flash monitor

# Monitor only (device already flashed, just watch logs)
idf.py -p COM5 monitor

# Clean build (if you suspect stale objects)
idf.py fullclean && idf.py build

# Set target (only needed after fullclean or fresh checkout)
idf.py set-target esp32p4
```

---

## Terminal Debugging Commands Reference

### Build & Flash

```bash
# Standard build
idf.py build

# Build with verbose output (show all compiler warnings)
idf.py build -v

# Build only specific component (faster iteration)
idf.py build audio

# Flash device
idf.py -p COM5 flash

# Flash with verbose output
idf.py -p COM5 flash -v

# Flash specific partition (bootloader, app, partition-table)
idf.py -p COM5 flash -ic="KEEP_EFUSE" bootloader

# Full clean + rebuild (fixes stale object files)
idf.py fullclean && idf.py build

# Clean only app, keep sdkconfig
idf.py clean

# Set target board (esp32p4)
idf.py set-target esp32p4

# Detect connected boards and list COM ports
idf.py monitor --list-ports
```

### Serial Monitor & Output Capture

```bash
# Open monitor on COM5
idf.py -p COM5 monitor

# Monitor with additional options
idf.py -p COM5 monitor --baudrate 115200

# Monitor and log to file simultaneously
idf.py -p COM5 monitor | tee monitor.log

# Monitor with timestamps
idf.py -p COM5 monitor 2>&1 | awk '{print strftime("%Y-%m-%d %H:%M:%S"), $0}'

# Monitor filtering (show only lines with [GRANT] or [DENY])
idf.py -p COM5 monitor 2>&1 | grep -E "\[GRANT\]|\[DENY\]"

# Monitor filtering (show errors and warnings)
idf.py -p COM5 monitor 2>&1 | grep -E "ERROR|WARN|error|warning"

# Monitor and count occurrences
idf.py -p COM5 monitor 2>&1 | grep -c "STATUS"

# Monitor with real-time tail of logfile
tail -f monitor.log

# Monitor using screen (alternative terminal method)
screen /dev/ttyUSB0 115200

# Monitor using minicom
minicom -D /dev/ttyUSB0 -b 115200
```

### Flash Combined Operations

```bash
# Build + Flash + Monitor (most common workflow)
idf.py -p COM5 flash monitor

# Build + Flash + Monitor with verbose output
idf.py -p COM5 flash monitor -v

# Erase flash completely (dangerous, resets board state)
idf.py -p COM5 erase-flash

# Erase and flash (clean slate)
idf.py -p COM5 erase-flash && idf.py -p COM5 flash monitor
```

### Configuration & Project Setup

```bash
# Interactive configuration menu
idf.py menuconfig

# Show current configuration
idf.py reconfigure

# Export environment to file
idf.py export

# Show CMake details
idf.py reconfigure --verbose
```

### ESP-IDF Tools & Environment

```bash
# Check IDF version
idf.py --version

# List available targets
idf.py dump-targets

# Show project info (description, targets, components)
idf.py info

# Check component details
idf.py info components

# Show build directory size
du -sh build/

# Show the binary size breakdown
idf.py size

# Show memory usage per component
idf.py size-components

# Install/check ESP-IDF tools
idf.py --list-targets

# Update component manager
compote list
```

### Log Analysis & Filtering

```bash
# Find all error lines in monitor output
idf.py -p COM5 monitor 2>&1 | grep -i "error"

# Find NFC-related messages
idf.py -p COM5 monitor 2>&1 | grep -E "PN532|NFC"

# Find API/HTTP messages
idf.py -p COM5 monitor 2>&1 | grep -E "HTTP|API|access"

# Find audio-related messages
idf.py -p COM5 monitor 2>&1 | grep -E "audio|tone|codec"

# Find Ethernet messages
idf.py -p COM5 monitor 2>&1 | grep -E "ETH|IP|DHCP"

# Find Telnet messages
idf.py -p COM5 monitor 2>&1 | grep -E "telnet|TELNET"

# Show lines with timestamps and filter for status updates
idf.py -p COM5 monitor 2>&1 | grep "STATUS" | awk '{print strftime("%H:%M:%S"), $0}'

# Count check events over time
idf.py -p COM5 monitor 2>&1 | tee -a session.log | grep -c "\[CHECK\]"

# Show only grant/deny decisions (access events)
idf.py -p COM5 monitor 2>&1 | grep -E "\[GRANT\]|\[DENY\]"

# Show reaction times from access logs
idf.py -p COM5 monitor 2>&1 | grep "react:" | sed 's/.*react: //'
```

### File & Code Analysis

```bash
# Search for string across all source files
grep -r "DEBUG_VERBOSE_LOGGING" main/

# Find all TODO or HACK comments
grep -r "TODO\|HACK\|FIXME" main/

# List all #include directives
grep -r "#include" main/

# Find all ESP_LOGI/ESP_LOGD/ESP_LOGW calls
grep -r "ESP_LOG" main/

# Count lines of code
wc -l main/*.c main/*.h

# Show file sizes
ls -lh main/*.c main/*.h

# Find binary files in build directory
find build/ -name "*.o" -o -name "*.a" | head -10

# Show compilation command details
cat build/compile_commands.json | grep -o '"command":"[^"]*"' | head -5
```

### Serial Port & Hardware Detection

```bash
# List all serial ports (Linux)
ls -la /dev/tty*

# List all serial ports (macOS)
ls -la /dev/cu.*

# Identify ESP32 USB device
lsusb | grep -i "Espressif\|Silicon Labs\|FTDI"

# Get device info (Linux)
udevadm info /dev/ttyUSB0

# Get device permissions
ls -la /dev/ttyUSB0

# Reset device via serial DTR signal
python:
import serial
s = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.1)
s.dtr = False
time.sleep(0.1)
s.dtr = True

# Monitor USB device changes
udevadm monitor --subsystem-match=tty
```

### Build Directory Inspection

```bash
# Show all generated files
ls -la build/

# List compiled object files
find build/ -name "*.o" -type f | wc -l

# Find largest object files
find build/ -name "*.o" -type f -exec ls -lh {} \; | sort -k5 -h | tail -10

# Show linker map (memory layout)
cat build/esp32p4_project.map | grep -A 5 "^.rodata"

# Check component includes
grep -r "target_include_directories" main/CMakeLists.txt
```

### Compilation & Linking Issues

```bash
# Rebuild with minimal output (show errors only)
idf.py build --compile-commands-only 2>&1 | grep "error:"

# Show all warnings during build
idf.py build 2>&1 | grep "warning:"

# Find undefined references
idf.py build 2>&1 | grep "undefined reference"

# Check if all SRCS are listed in CMakeLists.txt
ls main/*.c | wc -l
grep "\.c" main/CMakeLists.txt | wc -l

# Verify component is fetchable
idf.py manifest-show
```

### Git & Version Control (if applicable)

```bash
# Show uncommitted changes
git diff main/

# Show git status
git status

# View commit history
git log --oneline -10

# Check which files changed most recently
git log --name-status -5
```

### Network & Connectivity Debugging (on device IP)

```bash
# Ping device (IP shown in boot log — last seen: 10.10.5.23)
ping 10.10.5.23

# Telnet to device monitor
telnet 10.10.5.23 23

# Telnet with netcat (nc)
nc -v 10.10.5.23 23

# Check if port 23 open without connecting
nc -zv 10.10.5.23 23

# Monitor network packets to/from device (on host, requires tcpdump)
sudo tcpdump -i any host 10.10.5.23 -n
sudo tcpdump -i any host 10.10.5.23 and port 443 -n   # HTTPS only
```

### curl — API Testing Without the Device

> Replace `PASTE_TOKEN` with the actual value from `main/secrets.h`.
> These commands let you verify the server is working independently of the ESP32.

**PythonAnywhere card-access (`USE_PA_API 1`):**
```bash
# Known card — should return Sonuc:true
curl -s -X POST https://mbaykal.pythonanywhere.com/api/card-access \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer a1d76c1f45b9d653bcfe0c0782c928b6" \
  -d '{"mifareId":"4EF2AD15","terminalId":"203","include_photo":false}' \
  | python3 -m json.tool

# Unknown card — should return Sonuc:false
curl -s -X POST https://mbaykal.pythonanywhere.com/api/card-access \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer a1d76c1f45b9d653bcfe0c0782c928b6" \
  -d '{"mifareId":"DEADBEEF","terminalId":"203","include_photo":false}' \
  | python3 -m json.tool

# Token check — wrong token, should return 403
curl -s -o /dev/null -w "%{http_code}" -X POST \
  https://mbaykal.pythonanywhere.com/api/card-access \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer wrongtoken" \
  -d '{"mifareId":"4EF2AD15","terminalId":"203"}'
```

**PythonAnywhere status reporter:**
```bash
curl -s -X POST https://mbaykal.pythonanywhere.com/api/turnstile_status \
  -H "Content-Type: application/json" \
  -d '{
    "device_name": "Eczacılık Fakültesi 1",
    "nfc_online": true,
    "qr_online": true,
    "uptime_seconds": 60.0,
    "last_nfc_read_seconds_ago": null,
    "last_qr_read_seconds_ago": null
  }' | python3 -m json.tool
```

**Anadolu University API (`USE_PA_API 0`):**
```bash
# Replace PASTE_TOKEN with API_BEARER_TOKEN from secrets.h
curl -s -X POST https://anages.anadolu.edu.tr/api/dis-erisim/kart-erisim-arge \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer PASTE_TOKEN" \
  -d '{"mifareId":"4EF2AD15"}' \
  | python3 -m json.tool

# TLS reachability check only
curl -v --max-time 5 https://anages.anadolu.edu.tr/ 2>&1 | grep -E "Connected|SSL|TLS|error"
curl -v --max-time 5 https://mbaykal.pythonanywhere.com/ 2>&1 | grep -E "Connected|SSL|TLS|error"
```

### Monitor — Linux Port Names

> On Linux, replace `COM5` with your actual port (find it with `ls /dev/ttyUSB*`):

```bash
# Flash + monitor (Linux)
idf.py -p /dev/ttyUSB0 flash monitor

# Monitor only (Linux)
idf.py -p /dev/ttyUSB0 monitor

# Grant/deny events only
idf.py -p /dev/ttyUSB0 monitor 2>&1 | grep -E "\[GRANT\]|\[DENY\]|\[CHECK\]|\[ERROR\]"

# Show reaction times
idf.py -p /dev/ttyUSB0 monitor 2>&1 | grep "react:"

# Save full session log
idf.py -p /dev/ttyUSB0 monitor 2>&1 | tee ~/esp32_session_$(date +%Y%m%d_%H%M%S).log
```

### ESP-IDF Monitor — Print Filters (project-specific)

```bash
# NFC driver verbose, everything else warnings only
idf.py -p COM5 monitor --print-filter="pn532:V *:W"

# API + access check verbose
idf.py -p COM5 monitor --print-filter="pa_access:V access:V *:W"

# Status reporter + reporter verbose
idf.py -p COM5 monitor --print-filter="reporter:V *:W"

# Ethernet driver verbose
idf.py -p COM5 monitor --print-filter="eth_ip101:V esp_eth:V *:W"

# All project tags verbose, ESP-IDF internals silent
idf.py -p COM5 monitor --print-filter="main:V pn532:V pa_access:V access:V reporter:V tlog:V *:N"

# Errors only (quietest)
idf.py -p COM5 monitor --print-filter="*:E"
```

### Alternative Serial Terminals

```bash
# picocom (lightweight, good for ESP32)
picocom -b 115200 /dev/ttyUSB0
# Exit: Ctrl+A then Ctrl+X

# screen
screen /dev/ttyUSB0 115200
# Exit: Ctrl+A then K

# minicom
minicom -D /dev/ttyUSB0 -b 115200
# Exit: Ctrl+A then Q

# Raw cat (no terminal processing — good for scripting)
stty -F /dev/ttyUSB0 115200 raw && cat /dev/ttyUSB0
```

### Memory & Performance Profiling

```bash
# Show RAM usage breakdown
idf.py size-files

# Analyze memory by type
idf.py size-components

# Check partition table
esptool.py -p COM5 read_partition_table

# Read partition table from device
esptool.py -p COM5 --chip esp32p4 read_flash_status

# Estimate memory available
idf.py info memory
```

### Component Manager

```bash
# List all dependencies
idf.py manifest-show

# List dependency tree
idf.py manifest-validate

# Check espressif/esp_codec_dev version
idf.py manifest-show | grep esp_codec_dev

# Download without building
idf.py install
```

### Debugging with GDB (advanced)

```bash
# Build with debug symbols
idf.py build

# Start GDB debugger (requires JTAG setup)
idf.py gdb

# Connect to remote GDB (if JTAG via OpenOCD)
gdb build/esp32p4_project.elf

# In GDB console:
(gdb) target remote :3333        # Connect to OpenOCD
(gdb) monitor gdb_sync           # Sync with target
(gdb) break app_main             # Set breakpoint
(gdb) continue                   # Run to breakpoint
```

### SPI & Hardware-Level Debugging

```bash
# Monitor SPI transactions (Linux, requires spidev-test or logic analyzer output)
# Typically requires external tool or scope

# Simulate SPI read without device
python3 << 'EOF'
import spidev
spi = spidev.SpiDev()
spi.open(2, 0)      # bus 2, chip select 0
spi.max_speed_hz = 1000000
msg = [0x55] * 10   # PN532 wake preamble
spi.writebytes(msg)
EOF
```

### Quick Hardware Tests (add to code temporarily, then remove)

```c
// GPIO blink test (add to app_main.c)
for (int i = 0; i < 5; i++) {
    gpio_set_level(GPIO_NUM_1, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GPIO_NUM_1, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Amp control test (toggles GPIO53 → should hear click)
gpio_set_level(GPIO_NUM_53, 1);
vTaskDelay(pdMS_TO_TICKS(50));
gpio_set_level(GPIO_NUM_53, 0);

// UART loop test (add to gm805_uart.c)
uart_write_bytes(UART_NUM_1, "GM805 Test\r\n", 12);
```

---

## File Map — What to Look at for What

| Problem area | Primary file | Secondary file |
|---|---|---|
| NFC not reading | `main/pn532_spi.c` | `main/pn532_spi.h` |
| Barcode/QR not reading | `main/gm805_uart.c` | `main/gm805_uart.h` |
| No audio / wrong tones | `main/audio.c` | `main/config.h` (TONE_* defines) |
| No Ethernet / IP | `main/eth_ip101.c` | `main/config.h` (ETH_* pins) |
| API grant/deny wrong | `main/access_check.c` OR `main/pa_access_check.c` | `main/config.h` (USE_PA_API) |
| Status reporter failing | `main/status_reporter.c` | `main/config.h` (REPORTER_URL) |
| LED not firing | `main/app_main.c` | `main/config.h` (LED_*_PIN) |
| Door not opening (logic) | `main/app_main.c` (`handle_access`) | — |
| Telnet not connecting | `main/telnet_server.c` | `main/config.h` (TELNET_PORT) |
| Timing / heartbeat wrong | `main/config.h` (timing constants) | `main/app_main.c` |
| Secrets / tokens | `main/secrets.h` | `main/config.h` (API_TERMINAL_ID) |
| Build errors | `main/CMakeLists.txt` | `main/idf_component.yml` |
| OTA update | `main/ota.c` | — |

---

## Enable Verbose Logging

In `main/config.h`:
```c
#define DEBUG_VERBOSE_LOGGING   1   // 0 = production, 1 = verbose
```

Then rebuild and flash. Verbose mode exposes `ESP_LOGD` messages (SPI wire bytes,
HTTP response bodies, JSON parse steps, etc.).

---

## NFC (PN532) Not Working

### Symptoms and fixes

| Monitor log | Meaning | Fix |
|---|---|---|
| `PN532: FW=1.6` at boot | NFC ready — normal | — |
| `PN532 not responding` | SPI or power issue | Check DIP switches + wiring |
| `Bad ACK: 00 00 00 00` | SPI state machine confused | Power-cycle device |
| `STATUS 0x00` (LOGW once) | Clone chip, no status reg | Normal, fallthrough OK |
| `NFC: READY` missing | Init failed, in retry loop | Watch for recovery logs |

### DIP switch (on-board PN532 module)
```
SW1 = ON
SW2 = OFF
→ Selects SPI mode
```

### SPI pin check (`main/config.h`)
```c
#define NFC_SCK_PIN   GPIO_NUM_20
#define NFC_MISO_PIN  GPIO_NUM_22
#define NFC_MOSI_PIN  GPIO_NUM_21
#define NFC_CS_PIN    GPIO_NUM_23
```

### Quick SPI wire diagnostics
| MISO idle value | Meaning |
|---|---|
| `0x00` always | MISO stuck LOW — power or wiring problem |
| `0x80` | PN532 READY (correct) |
| `0xFF` | MISO floating — CS not asserting |
| `0x10` | SPI state machine corrupted |

### Recovery flow (automatic)
The firmware retries NFC every `NFC_RETRY_DELAY_MS` (5 s).
`pn532_reconfigure()` sends 10×`0x55` wake preamble + 1000 ms settle.
If recovery keeps failing → power-cycle the board.

### Relevant functions
| Function | File | What it does |
|---|---|---|
| `pn532_init()` | `pn532_spi.c` | Full init, SPI bus + wake + SAM |
| `pn532_get_firmware_version()` | `pn532_spi.c` | Ping to confirm chip alive |
| `pn532_reconfigure()` | `pn532_spi.c` | Wake preamble + SAM + MaxRetries |
| `pn532_read_passive_target()` | `pn532_spi.c` | Poll for card (called every loop) |
| `nfc_try_recover()` | `app_main.c` | Called when `s_nfc_ready == false` |

---

## QR / Barcode (GM805) Not Working

### Symptoms and fixes

| Symptom | Likely cause | Fix |
|---|---|---|
| No `[CHECK] Barcode:` in logs | GM805 not sending | Check RX/TX wiring or baud rate |
| Garbage characters | Wrong baud rate | Confirm `BARCODE_BAUD 115200` in config.h |
| Only first scan works | Duplicate suppression | Wait `CARD_READ_DELAY_MS` (2000 ms) between scans |

### UART pin check (`main/config.h`)
```c
#define BARCODE_RX_PIN   GPIO_NUM_4   // ESP32 RX ← GM805 TX
#define BARCODE_TX_PIN   GPIO_NUM_5   // ESP32 TX → GM805 RX
#define BARCODE_BAUD     115200
```

> Note: if header pins GPIO37/GPIO38 are wired instead, change to:
> ```c
> #define BARCODE_RX_PIN  GPIO_NUM_38
> #define BARCODE_TX_PIN  GPIO_NUM_37
> ```

### Relevant functions
| Function | File | What it does |
|---|---|---|
| `gm805_init()` | `gm805_uart.c` | UART1 init |
| `gm805_read_barcode()` | `gm805_uart.c` | Blocking read with timeout |

---

## Web API Problems

### Switch between APIs (`main/config.h`)
```c
#define USE_PA_API  0   // 0 = Anadolu University
                        // 1 = PythonAnywhere
```

### Anadolu University API

| Log | Meaning |
|---|---|
| `HTTP 200` + `[GRANT]` | OK |
| `HTTP 401` | Bearer token expired or wrong |
| `HTTP 403` | IP not whitelisted or token wrong |
| `HTTP 500` | Server error — retry later |
| `ESP_ERR_HTTP_FETCH_HEADER` | TLS dropped, will reconnect |

- Token: `API_BEARER_TOKEN` in `main/secrets.h` (valid until 2050)
- Endpoint: `API_URL` in `main/config.h`
- Terminal ID: `API_TERMINAL_ID "203"` in `main/config.h`
- Timeout: `API_TIMEOUT_MS 5000` — raise to `8000` if campus LAN is slow
- File: `main/access_check.c`

### PythonAnywhere API

| Log | Meaning |
|---|---|
| `HTTP 200` + `Sonuc:true` | OK |
| `HTTP 403` | Token wrong — check `PA_ACCESS_TOKEN` in `secrets.h` |
| `JSON parse failed` | Response too large (photo included) — add `include_photo:false` |
| `Keepalive failed` | Connection dropped — reconnects automatically at next card read |

- Token: `PA_ACCESS_TOKEN` in `main/secrets.h` (first 32 chars of app.secret_key)
- Endpoint: `PA_ACCESS_URL` in `main/config.h`
- File: `main/pa_access_check.c`
- The request body includes `"include_photo":false` to keep response small

### Status Reporter

| Log | Meaning |
|---|---|
| `Status sent (HTTP 200)` | OK — dashboard updated |
| `Status report failed` | Network issue — non-critical, retries at next 30 s heartbeat |

- Endpoint: `REPORTER_URL` in `main/config.h`
- Sent every `STATUS_HEARTBEAT_MS` (30 s)
- No auth token required
- File: `main/status_reporter.c`

### Persistent TLS connection explained
Both `access_check.c` and `pa_access_check.c` keep the TLS session alive.
- First request pays the TLS handshake cost (~500 ms)
- Subsequent card reads reuse the live connection (~100–400 ms)
- Keepalive POST runs every 30 s (piggybacked on heartbeat)
- On Ethernet drop: `*_deinit()` called → clean reconnect when link returns

---

## GPIO / LED Problems

### LED pins (`main/config.h`)
```c
#define LED_GREEN_PIN   GPIO_NUM_1   // door open / grant
#define LED_RED_PIN     GPIO_NUM_2   // denied
#define LED_ON_TIME_MS  1000         // auto-off after 1 s
```

### Quick GPIO toggle test (add temporarily to `app_main.c`)
```c
// Blinks green LED 3 times at boot to confirm GPIO works
for (int i = 0; i < 3; i++) {
    gpio_set_level(LED_GREEN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_GREEN_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
}
```

### Amp enable test (confirms NS4150B + speaker physically work)
```c
// Hear a click → amp + speaker are physically OK
gpio_set_level(GPIO_NUM_53, 1);   // amp ON
vTaskDelay(pdMS_TO_TICKS(100));
gpio_set_level(GPIO_NUM_53, 0);   // amp OFF → audible click
```

---

## Audio Problems

### No sound at all
1. Check `I2S_DOUT_PIN = GPIO_NUM_9` (NOT GPIO11 — wiki labels are reversed)
2. Check `PA_CTRL_PIN = GPIO_NUM_53` — must go HIGH to enable amp
3. Check `esp_codec_dev_set_out_vol(dev, 100)` is being called after open
4. I2S must start **before** I2C codec init (MCLK must be running for 50 ms first)
5. Do NOT replace `esp_codec_dev` component with manual I2C register writes — they won't work

### Distorted / buzzing tone
- Try different frequency in `config.h`: `TONE_DENY_HZ`
- Small speaker sweet spot: **600–1500 Hz**
- Below 500 Hz → bass distortion; at some frequencies → mechanical resonance ("zırıltı")

### Tone constants (`main/config.h`)
```c
#define TONE_GRANT_1_HZ    1000   // first grant beep
#define TONE_GRANT_2_HZ    1500   // second grant beep
#define TONE_GRANT_DUR_MS   200   // each beep duration
#define TONE_GRANT_GAP_MS   100   // silence between beeps
#define TONE_DENY_HZ        700   // deny buzz
#define TONE_DENY_DUR_MS    400
#define TONE_AMPLITUDE     28000  // 16-bit peak (max 32767)
#define TONE_FADE_SAMPLES    80   // ~5 ms fade prevents pops/clicks
```

---

## Ethernet Problems

| Symptom | Fix |
|---|---|
| `ETH: DOWN` forever | Check PoE switch or cable |
| `ETH: UP` but no IP | DHCP issue — check switch config |
| IP obtained but no API | DNS issue or firewall — try IP directly |
| `ETH: UP` briefly then drops | Power issue on PoE — check wattage |

### Ethernet pin check (`main/config.h`)
```c
#define ETH_MDC_GPIO      GPIO_NUM_31
#define ETH_MDIO_GPIO     GPIO_NUM_52
#define ETH_PHY_RST_GPIO  GPIO_NUM_51
#define ETH_PHY_ADDR      1
```

---

## Telnet Monitor

```bash
telnet 10.10.5.23 23   # replace with device IP shown in boot log
```

- Up to 3 simultaneous clients (`TELNET_MAX_CLIENTS 3`)
- Shows all `[CHECK]`, `[GRANT]`, `[DENY]`, `[ERROR]`, `STATUS` events in real time
- If connection drops immediately: normal (client FIN/RST handling)
- Port: `TELNET_PORT 23` in `main/config.h`
- File: `main/telnet_server.c`

---

## Build / Compile Errors

### Missing component
```
fatal error: esp_codec_dev.h: No such file or directory
```
→ Run `idf.py build` (first build auto-downloads `espressif/esp_codec_dev`)
→ Check `main/idf_component.yml` for the component declaration

### Undefined reference
→ Check `main/CMakeLists.txt` — source file must be listed under `SRCS`

### Wrong target
```
ERROR: Target esp32 is not supported
```
→ Run `idf.py set-target esp32p4`

### Check syntax without flashing
```bash
idf.py build 2>&1 | grep -E "error:|warning:"
```

---

## Timing Constants Reference (`main/config.h`)

| Constant | Value | Purpose |
|---|---|---|
| `CARD_READ_DELAY_MS` | 2000 | Duplicate suppression window |
| `NFC_HEALTH_CHECK_MS` | 30000 | PN532 firmware ping interval |
| `NFC_WATCHDOG_TIMEOUT_MS` | 60000 | Max silent time before reset |
| `NFC_RETRY_DELAY_MS` | 5000 | Wait between recovery attempts |
| `STATUS_HEARTBEAT_MS` | 30000 | Serial + telnet heartbeat + keepalive |
| `LED_ON_TIME_MS` | 1000 | Green/red LED auto-off |
| `API_TIMEOUT_MS` | 5000 | HTTP request timeout |
| `BARCODE_TIMEOUT_MS` | 1000 | UART read window |

---

## Secrets Reference (`main/secrets.h` — never commit)

| Constant | Purpose |
|---|---|
| `API_BEARER_TOKEN` | JWT for Anadolu University API (valid until 2050) |
| `PA_ACCESS_TOKEN` | Bearer token for PythonAnywhere card-access API (first 32 chars of app.secret_key) |

`main/secrets.h.template` provides the format without real values — safe to share.

---

## Reaction Time Diagnostics

Every `[GRANT]` and `[DENY]` log line includes `react: Xms` — time from card UID
detected to LED firing.

Expected ranges:
| Condition | Typical react time |
|---|---|
| Anadolu University API (campus LAN) | 150–300 ms |
| PythonAnywhere API | 300–600 ms |
| No Ethernet (skip) | < 1 ms |

Heartbeat (`STATUS` every 30 s) also shows:
```
Reaction (door open) — avg: Xms  min: Xms  max: Xms  n=X
```

---

## Checklist: Device Not Working at All

- [ ] PoE cable plugged in (powers the board)
- [ ] `ETH: UP` visible in monitor within 5 s of boot
- [ ] `NFC: READY` visible in boot log
- [ ] `PN532: FW=1.6` in boot log
- [ ] `PA_CTRL GPIO53 → HIGH` (amp on) in boot audio sequence
- [ ] `Status sent (HTTP 200)` in first heartbeat — confirms network + DNS + TLS work
- [ ] Token in `secrets.h` matches the active API (`USE_PA_API` setting)
