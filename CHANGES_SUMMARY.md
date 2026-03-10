# NFC Reader Fix - Changes Summary

## Changes Made to Fix NFC Reader Issues

### 1. SPI Frequency Adjustment (`main/config.h`)
**Changed:** Restored SPI frequency from 100 kHz (diagnostic) to 1 MHz (standard)
```c
// Before:
#define NFC_SPI_FREQ_HZ (100 * 1000)  // 100 kHz — diagnostic

// After:
#define NFC_SPI_FREQ_HZ (1000 * 1000)  // 1 MHz — standard operating speed
```
**Why:** 100 kHz was too slow for normal operation. 1 MHz is the standard speed for PN532 SPI communication.

### 2. Extended Wake Sequence (`main/pn532_spi.c`)
**Changed:** Increased wake preamble from 3 to 5 bytes
```c
// Before:
uint8_t wake[3] = { 0x55, 0x55, 0x55 };

// After:
uint8_t wake[5] = { 0x55, 0x55, 0x55, 0x55, 0x55 };
```
**Why:** Some PN532 clones need more wake bytes to properly synchronize their SPI state machine.

### 3. Longer Power-On Delay (`main/pn532_spi.c`)
**Changed:** Increased stabilization time from 500ms to 1000ms
```c
// Before:
vTaskDelay(pdMS_TO_TICKS(500));

// After:
vTaskDelay(pdMS_TO_TICKS(1000));
```
**Why:** Gives the PN532 more time to stabilize after power-on, especially important for clone chips.

### 4. Increased CS Setup Time (`main/pn532_spi.c`)
**Changed:** Doubled CS assertion delay from 100µs to 200µs
```c
// Before:
esp_rom_delay_us(100);

// After:
esp_rom_delay_us(200);
```
**Why:** Ensures CS is fully asserted before SPI clock starts, preventing timing issues.

### 5. Hardware Reset Support (`main/config.h` and `main/pn532_spi.c`)
**Added:** Optional hardware reset pin configuration
```c
// In config.h:
#define NFC_RST_PIN     GPIO_NUM_NC   // Optional: connect to PN532 RSTPD_N

// In pn532_spi.c:
bool pn532_hardware_reset(void) { ... }
```
**Why:** Allows for reliable hardware reset of PN532 if reset pin is connected.

### 6. Hardware Reset in Init Sequence (`main/pn532_spi.c`)
**Added:** Automatic hardware reset attempt during initialization
```c
// Step 4a: Try hardware reset if available
pn532_hardware_reset();
```
**Why:** Ensures PN532 starts from a known good state if reset pin is available.

## New Files Created

### 1. `NFC_TROUBLESHOOTING.md`
Comprehensive troubleshooting guide covering:
- Hardware connection checklist
- DIP switch configuration
- Power supply verification
- Diagnostic output analysis
- Common issues and solutions
- Advanced debugging techniques

### 2. `BUILD_AND_FLASH.md`
Quick reference for building and flashing:
- ESP-IDF installation instructions
- Build commands (VS Code and CLI)
- Configuration steps
- Serial monitor usage
- Troubleshooting build issues

### 3. `CHANGES_SUMMARY.md` (this file)
Documents all changes made to fix the NFC reader.

## What These Changes Fix

### Primary Issues Addressed:
1. **Slow SPI communication** - Restored to proper 1 MHz speed
2. **Insufficient wake sequence** - Extended for better clone chip compatibility
3. **Timing issues** - Increased delays for more reliable initialization
4. **No hardware reset** - Added optional reset capability

### Expected Improvements:
- More reliable PN532 detection on startup
- Better compatibility with clone PN532 boards
- Reduced "PN532 not responding" errors
- More stable long-term operation

## How to Use the Hardware Reset Feature (Optional)

If you want to use hardware reset for maximum reliability:

1. **Connect a wire** from an available ESP32 GPIO to PN532's RSTPD_N pin
   - Suggested: Use GPIO24 (or any free GPIO)

2. **Update config.h:**
   ```c
   #define NFC_RST_PIN     GPIO_NUM_24  // Change from GPIO_NUM_NC
   ```

3. **Rebuild and flash** the firmware

The firmware will automatically use hardware reset during initialization and recovery.

## Testing the Changes

After flashing the updated firmware:

1. **Watch serial output** for these messages:
   ```
   [pn532] PN532 SPI init: SCK=20 MISO=22 MOSI=21 CS=23  1000 kHz
   [pn532]   Waiting 1000 ms for PN532 power-on / stabilisation...
   [pn532]   Sending PN532 SPI wake preamble (0x55 x5)...
   [pn532] PN532: FW=1.6  IC=0x32  Support=0x01
   [pn532] PN532 SAM configured, MaxRetries=5 — ready to read cards
   ```

2. **Test card detection:**
   - Hold an NFC card near the antenna
   - Should see: `NFC card: UID=XXXXXXXX`
   - Green LED should light up

3. **Monitor health checks:**
   - Every 10 seconds: automatic health check
   - If PN532 fails, automatic recovery attempts
   - Watch for "[NFC] Recovery successful"

## If Still Not Working

If the NFC reader still doesn't work after these changes:

1. **Check hardware connections** (see NFC_TROUBLESHOOTING.md)
2. **Verify DIP switches:** SW1=ON, SW2=OFF
3. **Check power supply:** Stable 3.3V or 5V
4. **Review diagnostic output:** Look for wire byte patterns
5. **Try hardware reset:** Connect reset pin and update config
6. **Test with different card:** Some cards are ISO14443B (not supported)

## Next Steps

1. **Build and flash** the updated firmware
2. **Monitor serial output** during initialization
3. **Test with NFC cards**
4. **Check troubleshooting guide** if issues persist
5. **Consider hardware reset** for maximum reliability

## Additional Resources

- **NFC_TROUBLESHOOTING.md** - Detailed troubleshooting guide
- **BUILD_AND_FLASH.md** - Build and flash instructions
- **PN532 Datasheet** - For detailed timing specifications
- **ESP-IDF Documentation** - For ESP32-P4 specific information
