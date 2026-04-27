# INA219 Current Sensor — Wiring & Troubleshooting

Board: Waveshare ESP32-P4-WIFI6-POE-ETH  
Purpose: Measure current drawn from the 5V rail by the sensor board (PN532, GM861, LEDs)  
Measured result: ~147 mA idle, 5.09V rail

---

## Wiring

### INA219 module → ESP32-P4 (logic/power)

| INA219 Pin | Connects to | Notes |
|-----------|------------|-------|
| VCC | 3.3V header pin | Logic supply — do NOT use 5V |
| GND | GND | Common ground |
| SDA | **GPIO6** | I2C_NUM_1 — dedicated bus |
| SCL | **GPIO3** | I2C_NUM_1 — dedicated bus |

### INA219 module → power circuit (measurement)

```
Board 5V pin ──→ Vin+ ──[0.1Ω shunt inside INA219]──→ Vin- ──→ sensor board 5V supply
                                                                 (PN532, GM861, LEDs…)

GND must be shared: ESP32 GND ↔ INA219 GND ↔ sensor board GND
```

The shunt resistor sits **in series** with the 5V line. Current flows through it;
the INA219 measures the tiny voltage drop (mV range) and calculates current.

---

## Why GPIO6/GPIO3 and NOT GPIO7/GPIO8

**Do NOT connect INA219 SDA/SCL to GPIO7/GPIO8 (the ES8311 I2C bus).**

| What happens | Why |
|-------------|-----|
| `ESP_ERR_INVALID_STATE` on `i2c_master_transmit` | `esp_codec_dev` calls `esp_codec_dev_open()` which leaves I2C_NUM_0 in async transaction mode |
| Swapping SDA/SCL on GPIO7/8 doesn't help | The issue is bus state, not address or wiring |
| Using `i2c_master_bus_reset()` to clear state risks crashing ES8311 | Both devices share the same bus handle |

**Root cause:** `esp_codec_dev ≥1.3.4` uses async I2C internally. After `esp_codec_dev_open()` completes, the bus handle is in a state where synchronous `i2c_master_transmit()` calls return `ESP_ERR_INVALID_STATE` even on a freshly added device.

**Fix:** Give INA219 its own independent `I2C_NUM_1` bus on free GPIOs. `ina219.c` calls `i2c_new_master_bus()` directly — no dependency on `audio_get_i2c_bus()`.

---

## Firmware

| File | Role |
|------|------|
| `main/ina219.h` | Public API: `ina219_init()`, `ina219_read()` |
| `main/ina219.c` | Driver — owns I2C_NUM_1 bus, calibrates on init |
| `main/config.h` | `INA219_SDA_PIN GPIO_NUM_6`, `INA219_SCL_PIN GPIO_NUM_3` |
| `main/app_main.c` | `ina219_init()` after `audio_init()`; read in `print_heartbeat()` |

### Calibration

```
R_shunt = 0.1 Ω  (standard INA219 module)
Current_LSB = 0.1 mA
Cal register = trunc(0.04096 / (Current_LSB_A × R_shunt))
             = trunc(0.04096 / (0.0001 × 0.1))
             = 4096  (0x1000)
```

Range: 0–3276.7 mA. Adequate for this sensor board (measured ~150–400 mA depending on activity).

### Heartbeat output (every 30 s)

```
5V rail: 5.09V   Current: 147 mA   Power: 0.75 W
```

### Non-fatal init

If INA219 is not wired or not responding, `ina219_init()` logs a warning and returns.
`ina219_read()` then returns `ESP_ERR_INVALID_STATE` and the heartbeat skips the line.
No crash. No effect on NFC/barcode/audio/API functionality.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `ESP_ERR_INVALID_STATE` on GPIO7/8 | I2C_NUM_0 in async mode after codec init | Use GPIO6/GPIO3 (I2C_NUM_1) |
| `No I2C devices found` scan result | VCC/GND/SDA/SCL not making contact | Check each wire; ensure VCC=3.3V not 5V |
| `ESP_ERR_NOT_FOUND` after scan | INA219 at unexpected address | Check A0/A1 solder bridges on module; default is 0x40 |
| Reading shows 0.00V 0 mA | Calibration register not written | Re-flash; check `write_reg(REG_CALIBRATION, 4096)` log |
| Current reads negative | Vin+ and Vin- swapped | Swap the two measurement wires |
| Voltage reads ~0V, current OK | Vin+ not connected to supply | Connect Vin+ to 5V source (not load side) |

---

## I2C address

Default (A0=GND, A1=GND): **0x40**

If address needs to change (e.g. second INA219), bridge A0/A1 on the module:

| A1 | A0 | Address |
|----|----|---------| 
| GND | GND | 0x40 (default) |
| GND | VCC | 0x41 |
| VCC | GND | 0x44 |
| VCC | VCC | 0x45 |
