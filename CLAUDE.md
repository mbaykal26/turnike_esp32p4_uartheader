# ESP32-P4 Access Control — Claude Context File

> Quick-reference for AI-assisted development sessions.
> Board: Waveshare ESP32-P4-WIFI6-POE-ETH

---

## Build & Flash

```bash
cd d:\ESP32_P4_access_idf_uart_header

idf.py set-target esp32p4          # only once per clean checkout
idf.py build                        # first run downloads esp_codec_dev
idf.py -p COM5 flash monitor        # adjust COM port as needed
```

First build downloads `espressif/esp_codec_dev` from the component registry.

---

## All Pin Assignments (single source of truth: main/config.h)

| Peripheral | Signal | GPIO | Notes |
|-----------|--------|------|-------|
| PN532 NFC | SCK | 20 | SPI2_HOST, 1 MHz, software bit-reversal |
| | MISO | 22 | |
| | MOSI | 21 | |
| | CS | 23 | Active LOW |
| GM805 Barcode | RX | 4 | UART1, 115200 baud |
| | TX | 5 | |
| ES8311 Codec | SDA | 7 | I2C_NUM_0 |
| | SCL | 8 | |
| I2S Audio | MCLK | 13 | Hardwired to ES8311 on PCB |
| | BCLK | 12 | |
| | LRCK | 10 | |
| | **DOUT** | **9** | → ES8311 DSDIN (DAC) ← KEY PIN |
| | DIN | 11 | ← ES8311 ASDOUT (ADC) |
| NS4150B Amp | PA_CTRL | 53 | HIGH = amp ON |
| LED Green | OUT | 1 | Door open signal |
| LED Red | OUT | 2 | |
| IP101 PHY | MDC | 31 | Ethernet management |
| | MDIO | 52 | |
| | RST | 51 | |

### ⚠️ Known Pin Gotcha
The Waveshare wiki labels DOUT=GPIO11 and DIN=GPIO9 — these are from the **ES8311 codec's perspective**.
From the **ESP32 I2S driver's perspective**, it is the opposite:
- ESP32 I2S `dout` (what the ESP32 **sends**) = **GPIO9** → goes into ES8311's DSDIN (DAC input)
- Setting `dout=GPIO11` would push audio into the ADC output pin → **silence**

### ⚠️ UART Pin Conflict — Serial Monitor Goes Silent After Startup Beep
GPIO37 and GPIO38 are the **console UART pins** (USB-UART bridge, used by `idf.py monitor`).
**Do NOT assign BARCODE_RX/TX to GPIO37/38** — UART1 init on those pins steals them from the
console and kills all monitor output. Symptom: monitor dies exactly after startup beep (step 3 init).

**Correct pins (confirmed working):** GPIO4 (RX) and GPIO5 (TX) — as set in config.h.

---

## PN532 DIP Switch Setting
Board-mounted PN532 breakout: **SW1=ON, SW2=OFF** → selects SPI mode.

---

## Component Dependencies
```
espressif/esp_codec_dev  ← declared in main/idf_component.yml
                           (auto-downloaded on first build)
```
All other components (esp_eth, esp_http_client, lwip, etc.) are built-in ESP-IDF.

---

## API Backend Selection

Toggle with one line in `config.h`:

```c
#define USE_PA_API  0   // 0 = Anadolu University API
                        // 1 = PythonAnywhere API
```

| API | File | Endpoint |
|-----|------|----------|
| Anadolu University | `access_check.c` | `https://anages.anadolu.edu.tr/api/dis-erisim/kart-erisim-arge` |
| PythonAnywhere | `pa_access_check.c` | `https://mbaykal.pythonanywhere.com/api/card-access` |

Both use a **persistent TLS connection** (init/keepalive/deinit pattern) — only the first request pays the TLS handshake cost.

### Anadolu University API
```
Request:  POST {"mifareId":"<UID_OR_BARCODE>"}
Response: {"Sonuc":true/false, "Ad":"Name", "Soyad":"Surname"}
Auth:     Bearer JWT (API_BEARER_TOKEN in secrets.h — valid until 2050)
```

### PythonAnywhere Card-Access API
```
Request:  POST {"mifareId":"<UID_OR_BARCODE>", "terminalId":"203"}
Response: {"Sonuc":true/false, "Mesaj":"...", "name":"Full Name"}
Auth:     Bearer token (PA_ACCESS_TOKEN in secrets.h)
```

---

## How to Switch API Endpoint (URL / terminalId / request body / response parser)

When testing a new endpoint or reverting to an old one, there are **4 places** to update — all in `main/access_check.c` and `main/config.h`. Do them in order.

### Step 1 — URL  (`main/config.h` lines 113–117)

Comment out the current `#define API_URL` and add the new one:
```c
// OLD (dis-erisim):
// #define API_URL  "https://anages.anadolu.edu.tr/api/dis-erisim/kart-erisim-arge"

// CURRENT (online-erisim):
#define API_URL \
    "https://anages.anadolu.edu.tr/api/terminal/online-erisim"
```

### Step 2 — Terminal ID  (`main/config.h` lines 122–124)

Comment out the current value and add the new one:
```c
// #define API_TERMINAL_ID     "203"          // dis-erisim terminal
#define API_TERMINAL_ID     "75379662"        // online-erisim terminal
```

> **terminalId format in JSON body matters:**
> - `online-erisim` → must be a **JSON number**: `"terminalId":75379662`  (no quotes)
> - `dis-erisim`    → must be a **JSON string**: `"terminalId":"203"`     (with quotes)
>
> The format is controlled in the body snprintf — see Step 3.

### Step 3 — Request body  (`main/access_check.c` lines 246–250 and 196–200)

Two places: the **access check body** and the **keepalive body** (they must stay in sync).

**Access check body** (line 248–250):
```c
// online-erisim: terminalId as JSON number, includes zaman
snprintf(body, sizeof(body),
         "{\"terminalId\":%s,\"mifareId\":\"%s\",\"zaman\":\"%s\"}",
         API_TERMINAL_ID, uid_str, zaman);

// dis-erisim: terminalId as quoted string, mifareId first
// snprintf(body, sizeof(body),
//          "{\"mifareId\":\"%s\",\"terminalId\":\"%s\",\"zaman\":\"%s\"}",
//          uid_str, API_TERMINAL_ID, zaman);
```

**Keepalive body** (line 198–200) — same pattern, empty mifareId:
```c
// online-erisim:
snprintf(body, sizeof(body),
         "{\"terminalId\":%s,\"mifareId\":\"\",\"zaman\":\"%s\"}",
         API_TERMINAL_ID, zaman);

// dis-erisim:
// snprintf(body, sizeof(body),
//          "{\"mifareId\":\"\",\"terminalId\":\"%s\",\"zaman\":\"%s\"}",
//          API_TERMINAL_ID, zaman);
```

### Step 4 — Response parser  (`main/access_check.c` lines 286–312)

Each endpoint returns different JSON field names for the grant decision and name:

| Endpoint | Grant field | Value when granted | Name field |
|----------|------------|-------------------|------------|
| `online-erisim` | `erisimSonucApiViewModel` | `"ERISIM_KABUL_EDILDI"` (string) | `isim` |
| `dis-erisim` | `Sonuc` | `true` (bool) | `Ad` + `Soyad` (separate) |

Switch the active `cJSON_GetObjectItem` call and the field names for `isim`/`mesaj` accordingly.
The old parsers are preserved in comments directly above the active code.

> **Also update `access_check.h`** (lines 10–16 and struct definition lines 24–37)
> if the result struct fields change (e.g. reverting to `first_name`/`last_name`).

### Quick-reference: current state

| Item | Current value | File:line |
|------|--------------|-----------|
| API_URL | `terminal/online-erisim` | `config.h:116` |
| API_TERMINAL_ID | `75379662` | `config.h:124` |
| Request body | `terminalId` as number + `zaman` | `access_check.c:248` |
| Keepalive body | same format, empty mifareId | `access_check.c:198` |
| Grant field | `erisimSonucApiViewModel` | `access_check.c:300` |
| Name field | `isim` | `access_check.c:307` |

---

## PythonAnywhere Status Reporter

Every 30 s (piggybacked on heartbeat), the device POSTs status to the dashboard:
```
POST https://mbaykal.pythonanywhere.com/api/turnstile_status
Body: {
    "device_name":               "Eczacılık Fakültesi 1",
    "nfc_online":                true/false,
    "qr_online":                 true/false,
    "uptime_seconds":            <float>,
    "last_nfc_read_seconds_ago": <float or null>,   // null = no card read yet
    "last_qr_read_seconds_ago":  <float or null>
}
```
`last_nfc_read_seconds_ago` is based on **actual card reads only** (`s_nfc_last_card_read`),
not NFC watchdog resets (`s_nfc_last_activity`). These are separate variables.

---

## Audio Tones & Voice Phrases

| Event | Tone | Voice (TTS) |
|-------|------|-------------|
| Boot startup | GRANT sequence (confirms speaker works) | — |
| Access GRANTED | 1000 Hz (200 ms) → silence (100 ms) → 1500 Hz (200 ms) | "Hoşgeldiniz" |
| Access DENIED | 700 Hz (400 ms) | "Kartınızı kontrol ediniz" |

Tones are **synchronous** (blocking, ~600 ms grant / ~430 ms deny).
Voice phrases run in a **FreeRTOS background task** (`tts_bg`, priority 2) so the main loop is free immediately after the tone. If a new card is scanned while voice is still playing, `abort_tts()` cuts it within ≤50 ms before the new tone starts.

### TTS Data Generation

```bash
cd tools
python gen_tts.py        # regenerate main/tts_data.h
# requires: pip install edge-tts pydub
#           winget install ffmpeg
```

- Engine: Microsoft Neural TTS (`edge-tts`), voice `tr-TR-EmelNeural`
- Output: 16 kHz, 16-bit mono PCM, normalized to −3 dBFS
- Stored in `main/tts_data.h` as `static const int16_t` C arrays
- Re-run whenever phrases change; re-run `idf.py build` afterwards

---

## Telnet Monitor
```bash
telnet <device-ip> 23
```
Shows real-time access events, status heartbeat every 30 s, up to 3 clients.

Sample output:
```
[CHECK] NFC: A3F2C1B0
[GRANT] User: Ali Veli  UID: A3F2C1B0  react: 187 ms
──────────────────────────────────────────
STATUS  Uptime: 00h 12m 05s
        Reaction (door open) — avg: 191 ms  min: 174 ms  max: 223 ms  n=12
──────────────────────────────────────────
```

---

## Reaction Time Measurement

`handle_access()` records `t_detect = now_ms()` the moment a card UID is identified.
`react_ms = now_ms() - t_detect` is computed right before the LED fires.

- Logged on every `[GRANT]` and `[DENY]` line in telnet
- Grant stats tracked: count, min, max, rolling average
- Displayed in the 30 s heartbeat: `Reaction (door open) — avg: X ms  min: X ms  max: X ms  n=X`

---

## Timing Constants (all in config.h)
| Constant | Value | Purpose |
|---------|-------|---------|
| CARD_READ_DELAY_MS | 2000 | Duplicate detection window |
| NFC_HEALTH_CHECK_MS | 30000 | PN532 firmware ping interval |
| NFC_RETRY_DELAY_MS | 5000 | Wait between recovery attempts |
| STATUS_HEARTBEAT_MS | 30000 | Serial/telnet heartbeat |
| LED_ON_TIME_MS | 1000 | Auto-off after grant/deny |
| API_TIMEOUT_MS | 5000 | HTTP request timeout (was 10000) |

---

## Secrets (main/secrets.h — never commit)
| Constant | Purpose |
|----------|---------|
| `API_BEARER_TOKEN` | JWT for Anadolu University API — valid until 2050 |
| `PA_ACCESS_TOKEN` | Bearer token for PythonAnywhere card-access API |

---

## Common Issues

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No audio | DOUT/DIN swapped | Check I2S_DOUT_PIN=GPIO9 in config.h |
| Audio too quiet | ES8311 volume | esp_codec_dev_set_out_vol(dev, 100) |
| TTS voice missing / silent | tts_data.h not generated | Run `tools/gen_tts.py`, then `idf.py build` |
| TTS cuts off mid-word | abort_tts() timeout too short | Increase poll count in abort_tts() (default 5×10 ms) |
| Compile error: play_pcm_mono undeclared | Missing forward declaration | Check forward decl before tts_bg_task in audio.c |
| PN532 "not responding" | DIP switches wrong | Set SW1=ON SW2=OFF (SPI mode) |
| PN532 "not responding" | SPI bit order | Uses software `rev_byte()` — do NOT add SPI_DEVICE_BIT_LSBFIRST |
| No Ethernet | RMII clock | IP101 PHY must be powered; check RST=GPIO51 |
| Telnet disconnects | Socket pruning | Normal — client sent FIN/RST |
| `last_nfc_read` wrong | Using wrong timestamp | Use `s_nfc_last_card_read`, not `s_nfc_last_activity` |

---

## ⚠️ Hard-Won Lessons — Audio (DO NOT skip this section)

The audio code in `audio.c` is the result of a long debugging journey through 4 versions
in the `ESP32_P4_speaker_idf` project. Audio was **completely silent** for v1–v3.
These lessons explain *why* the code is written the way it is.

### Why `espressif/esp_codec_dev` component is mandatory

**v1–v3 all used manual I2C register writes to configure ES8311.**
Even with correct register values (verified against the official Espressif driver), there was **no sound**.

Root cause discovered: Manual register writes miss the **analog output path initialisation**:
- VMID reference voltage startup sequence
- Headphone driver stage configuration
- Internal analog routing from DAC to output pins

The `esp_codec_dev` component handles all of this internally.
**Do not replace the component with manual register writes — they will not work.**

### Why the init order is I2S first, then codec

The ES8311 requires the **MCLK signal to be running** before it will respond to I2C configuration.
The code starts I2S (which drives MCLK=GPIO13) and waits 50 ms before touching the codec.
Reversing this order = codec ignores configuration = silence.

### Why DOUT=GPIO9 (not GPIO11 as the wiki says)

The Waveshare wiki pin table says: `DOUT=GPIO11, DIN=GPIO9`
These labels are written **from the ES8311 codec's perspective**:
- ES8311 DSDIN (its serial data INPUT for DAC) is on GPIO9
- ES8311 ASDOUT (its serial data OUTPUT from ADC) is on GPIO11

The ESP32 I2S driver uses **the opposite perspective**:
- ESP32 I2S `dout` = the pin the ESP32 **transmits on** = must connect to ES8311's INPUT = **GPIO9**
- Setting `dout=GPIO11` drives the ES8311's ADC output pin — an output that ignores incoming data — DAC receives nothing = silence

**This was the primary root cause of v1–v3 silence.**

### PA toggle test — hardware verification method

If you suspect a hardware problem (amp or speaker dead), you can verify the physical chain
**without involving the codec at all** by toggling GPIO53 (PA_CTRL) rapidly:

```c
// Quick test: if you hear clicks → amp + speaker physically work
gpio_set_level(GPIO_NUM_53, 1);   // amp ON
vTaskDelay(pdMS_TO_TICKS(100));
gpio_set_level(GPIO_NUM_53, 0);   // amp OFF  → audible click on toggle
```

During debugging, we ran this test and heard clicks — confirming the NS4150B amp and speaker
were physically functional. The problem was entirely in the codec software configuration.

### `channel_mask = 0x03` — not the macro

The `esp_codec_dev` API has a macro `ESP_CODEC_DEV_MAKE_CHANNEL_MASK` for setting channel_mask.
**This macro is undefined in the version we use (≥1.3.4)** and causes a compile error.
Use the raw value `0x03` instead (means both L and R channels active).

### Volume: `esp_codec_dev_set_out_vol(dev, 100)` = full volume

The ES8311 DAC volume register (0x32) has a non-obvious scale:
- 0x00 = 0 dB (maximum)
- Each step = −0.5 dB
- 0xBF and above = nearly silent

The `esp_codec_dev` API normalises this to 0–100 where 100 = maximum.
Always call `esp_codec_dev_set_out_vol(dev, 100)` after `esp_codec_dev_open()`.

### Fade-in/fade-out on sine wave tones

Without fade (abrupt start/stop), the speaker produces loud clicks/pops.
`audio.c` applies an 80-sample (~5 ms at 16 kHz) linear ramp at the start and end of every tone.
The amp (GPIO53) stays HIGH for the entire grant sequence (both tones + gap) to avoid amp-switching clicks.
