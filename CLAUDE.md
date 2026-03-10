# ESP32-P4 Access Control — Claude Context File

> Quick-reference for AI-assisted development sessions.
> Board: Waveshare ESP32-P4-WIFI6-POE-ETH

---

## Build & Flash

```bash
cd C:\Users\Murat\Desktop\ESP32_P4_access_idf

idf.py set-target esp32p4          # only once per clean checkout
idf.py build                        # first run downloads esp_codec_dev
idf.py -p COM5 flash monitor        # adjust COM port as needed
```

First build downloads `espressif/esp_codec_dev` from the component registry.

---

## All Pin Assignments (single source of truth: main/config.h)

| Peripheral | Signal | GPIO | Notes |
|-----------|--------|------|-------|
| PN532 NFC | SCK | 20 | SPI2_HOST, 1 MHz, LSB-first |
| | MISO | 21 | |
| | MOSI | 22 | |
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
| LED Green | OUT | 1 | |
| LED Red | OUT | 2 | |
| IP101 PHY | MDC | 31 | Ethernet management |
| | MDIO | 52 | |
| | RST | 51 | |

### ⚠️ Known Pin Gotcha
The Waveshare wiki labels DOUT=GPIO11 and DIN=GPIO9 — these are from the **ES8311 codec's perspective**.
From the **ESP32 I2S driver's perspective**, it is the opposite:
- ESP32 I2S `dout` (what the ESP32 **sends**) = **GPIO9** → goes into ES8311's DSDIN (DAC input)
- Setting `dout=GPIO11` would push audio into the ADC output pin → **silence**

### Optional: GM805 alternative pins
If GPIO37(TXD)/GPIO38(RXD) on the header are not wired to the USB-UART bridge chip:
```c
// In config.h, change only these two lines:
#define BARCODE_RX_PIN   GPIO_NUM_38
#define BARCODE_TX_PIN   GPIO_NUM_37
```
Verify by checking if `idf.py monitor` still prints after changing (no UART conflict).

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

## API Endpoint
```
POST https://anages.anadolu.edu.tr/api/dis-erisim/kart-erisim-arge
Body:     {"mifareId":"<UID_OR_BARCODE>"}
Response: {"Sonuc":true/false, "Ad":"Name", "Soyad":"Surname"}
```
Bearer JWT in `config.h` — valid until 2050.

---

## Audio Tones
| Event | Tone |
|-------|------|
| Boot startup | GRANT sequence (confirms speaker works) |
| Access GRANTED | 1000 Hz (200 ms) → silence (100 ms) → 1500 Hz (200 ms) |
| Access DENIED | 400 Hz (400 ms) |

---

## Telnet Monitor
```bash
telnet <device-ip> 23
```
Shows real-time access events, status heartbeat every 30 s, up to 3 clients.

---

## Timing Constants (all in config.h)
| Constant | Value | Purpose |
|---------|-------|---------|
| CARD_READ_DELAY_MS | 2000 | Duplicate detection window |
| NFC_HEALTH_CHECK_MS | 10000 | PN532 firmware ping interval |
| NFC_RETRY_DELAY_MS | 5000 | Wait between recovery attempts |
| STATUS_HEARTBEAT_MS | 30000 | Serial/telnet heartbeat |
| LED_ON_TIME_MS | 1000 | Auto-off after grant/deny |

---

## Common Issues

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No audio | DOUT/DIN swapped | Check I2S_DOUT_PIN=GPIO9 in config.h |
| Audio too quiet | ES8311 volume | esp_codec_dev_set_out_vol(dev, 100) |
| PN532 "not responding" | DIP switches wrong | Set SW1=ON SW2=OFF (SPI mode) |
| PN532 "not responding" | SPI bit order | Must use SPI_DEVICE_BIT_LSBFIRST |
| No Ethernet | RMII clock | IP101 PHY must be powered; check RST=GPIO51 |
| Telnet disconnects | Socket pruning | Normal — client sent FIN/RST |

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
