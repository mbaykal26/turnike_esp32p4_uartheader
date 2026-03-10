# PN532 NFC Reader Troubleshooting Guide

## Quick Checklist

### 1. Hardware Connections
Verify these connections between ESP32-P4 and PN532:
- **SCK** (GPIO20) → PN532 SCK
- **MISO** (GPIO22) → PN532 MISO (SDO)
- **MOSI** (GPIO21) → PN532 MOSI (SDI)
- **CS** (GPIO23) → PN532 SS/NSS
- **VCC** → 3.3V or 5V (check your board's voltage requirements)
- **GND** → GND

### 2. DIP Switch Configuration
The PN532 board MUST be in SPI mode:
- **SW1 = ON** (towards the ON marking)
- **SW2 = OFF** (towards the OFF marking)

This is critical - wrong DIP switch settings are the most common issue.

### 3. Power Supply
- Ensure stable 3.3V or 5V supply (check PN532 board specs)
- Some PN532 boards have a voltage regulator jumper - verify it's set correctly
- Check for sufficient current capacity (PN532 can draw 100-150mA during RF operation)

## Diagnostic Output Analysis

When you run the firmware, check the serial output for these diagnostic messages:

### Good Signs
```
PN532: FW=1.6  IC=0x32  Support=0x01
PN532 SAM configured, MaxRetries=5 — ready to read cards
```

### Problem Indicators

#### "PN532 not responding after 3 attempts"
- Check all wiring connections
- Verify DIP switches (SW1=ON, SW2=OFF)
- Check power supply voltage and stability
- Try connecting a hardware reset pin

#### STATUS wire byte diagnostics:
- **0x80** = Ready (good!)
- **0x00 0x00** = MISO stuck LOW → Check MISO wiring or PN532 power
- **0xFF 0xFF** = MISO floating HIGH → CS not asserting properly, check CS wiring
- **0x10 0x10** = PN532 stuck in bad state → Power cycle the board

#### "Bad ACK"
- Communication timing issue
- Try reducing SPI frequency (already at 1 MHz)
- Check for loose connections
- Verify ground connection is solid

## Recent Code Improvements

The code has been updated with:

1. **Increased SPI frequency** from 100 kHz to 1 MHz (standard speed)
2. **Extended wake sequence** with 5x 0x55 bytes instead of 3
3. **Longer power-on delay** (1000ms instead of 500ms)
4. **Hardware reset support** (optional, requires connecting reset pin)

## Hardware Reset Option

For more reliable operation, you can connect the PN532's RSTPD_N pin to an ESP32 GPIO:

1. Choose an available GPIO (e.g., GPIO24)
2. Update `config.h`:
   ```c
   #define NFC_RST_PIN     GPIO_NUM_24
   ```
3. Connect GPIO24 → PN532 RSTPD_N pin
4. The firmware will automatically use hardware reset during initialization

## Testing Steps

1. **Power cycle everything**
   - Disconnect power from both ESP32-P4 and PN532
   - Wait 10 seconds
   - Reconnect power

2. **Check serial output**
   - Look for "PN532 SPI init" message
   - Watch for diagnostic wire bytes
   - Verify firmware version is detected

3. **Test with a card**
   - Hold an NFC card/tag near the PN532 antenna
   - Should see "NFC card: UID=..." in the logs
   - Green LED should light up (if access granted)

4. **Monitor health checks**
   - Every 10 seconds, the firmware checks PN532 health
   - If it fails, automatic recovery attempts will be made
   - Watch for "[NFC] Recovery successful" messages

## Common Issues and Solutions

### Issue: "NFC watchdog: no activity for 30s"
**Solution:** This is normal if no cards are being scanned. The firmware will re-check the PN532 automatically.

### Issue: Cards not detected
**Possible causes:**
- PN532 antenna not properly connected
- Card too far from antenna (try < 3cm distance)
- Wrong card type (PN532 supports ISO14443A: MIFARE, NTAG, etc.)
- RF field not enabled (SAM configuration failed)

**Try:**
- Check antenna connection to PN532 board
- Try a different NFC card/tag
- Look for "SAM configuration failed" in logs

### Issue: Intermittent detection
**Possible causes:**
- Loose wiring connections
- Power supply noise/instability
- SPI bus interference

**Try:**
- Use shorter wires (< 20cm recommended)
- Add 0.1µF capacitor near PN532 VCC pin
- Keep wires away from power lines and motors
- Reduce SPI frequency to 500 kHz in config.h

### Issue: Works after power-on but stops later
**Possible causes:**
- PN532 entering power-down mode
- SPI bus corruption
- Thermal issues

**Try:**
- The firmware has automatic recovery - check if it recovers
- Ensure good ventilation around PN532
- Check for "[NFC] Recovery successful" in logs

## Advanced Debugging

### Enable Extended Diagnostics
The firmware already includes diagnostic mode during initialization. To see raw SPI bytes:

1. The first 30 SPI transactions are logged automatically
2. Look for "[diag#XX] TX wire:" and "RX wire:" messages
3. Compare with expected values in the code comments

### SPI Signal Quality
If you have an oscilloscope or logic analyzer:
- Check SCK signal is clean square wave at 1 MHz
- Verify CS goes LOW before SCK starts
- MOSI/MISO should show data transitions synchronized with SCK
- Look for ringing or reflections on long wires

### Firmware Version Check
The PN532 should report:
- **IC = 0x32** (PN532 chip identifier)
- **FW = 1.6** (most common firmware version)

If you see different values, you might have a clone chip or different firmware.

## ESP-IDF Extension

You mentioned needing the ESP-IDF extension. To build and flash this project:

### Using VS Code with ESP-IDF Extension:
1. Install the ESP-IDF extension from VS Code marketplace
2. Open this project folder
3. Press `Ctrl+E` then `D` to select your ESP32-P4 target
4. Press `Ctrl+E` then `B` to build
5. Press `Ctrl+E` then `F` to flash
6. Press `Ctrl+E` then `M` to open serial monitor

### Using Command Line:
```bash
# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Configure for ESP32-P4
idf.py set-target esp32p4

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Still Not Working?

If you've tried everything above:

1. **Test with a known-good PN532 board** (if available)
2. **Try a different SPI bus** (change NFC_SPI_HOST to SPI3_HOST and update pins)
3. **Check for counterfeit PN532 chips** (some clones have quirks)
4. **Measure voltages** with a multimeter:
   - VCC should be stable 3.3V or 5V
   - Check voltage doesn't drop when PN532 activates RF field
5. **Post diagnostic output** to get help - include:
   - Full serial output from boot
   - Wire byte diagnostics
   - Photo of your wiring setup
