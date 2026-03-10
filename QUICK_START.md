# Quick Start - NFC Reader Fix

## 🚀 Immediate Actions

### 1. Hardware Check (2 minutes)
- [ ] **PN532 DIP Switches:** SW1=ON, SW2=OFF
- [ ] **Wiring:**
  - GPIO20 → PN532 SCK
  - GPIO21 → PN532 MOSI
  - GPIO22 → PN532 MISO  
  - GPIO23 → PN532 CS
  - 3.3V/5V → PN532 VCC
  - GND → PN532 GND
- [ ] **Power:** Stable voltage, no brownouts

### 2. Build & Flash (5 minutes)

#### Using VS Code + ESP-IDF Extension:
```
Ctrl+E → D  (select esp32p4)
Ctrl+E → B  (build)
Ctrl+E → F  (flash)
Ctrl+E → M  (monitor)
```

#### Using Command Line:
```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 3. Verify (1 minute)
Look for this in serial output:
```
[pn532] PN532: FW=1.6  IC=0x32  Support=0x01
[pn532] PN532 SAM configured, MaxRetries=5 — ready to read cards
```

✅ **Success!** Try scanning an NFC card.

❌ **Failed?** See troubleshooting below.

---

## 🔧 What Was Fixed

The code now has:
1. ✅ Proper 1 MHz SPI speed (was 100 kHz)
2. ✅ Extended wake sequence (5 bytes instead of 3)
3. ✅ Longer power-on delay (1000ms instead of 500ms)
4. ✅ Increased CS setup time (200µs instead of 100µs)
5. ✅ Optional hardware reset support

---

## ⚠️ Common Issues & Quick Fixes

### "PN532 not responding"
**Fix:** Check DIP switches (SW1=ON, SW2=OFF) and power supply

### "Bad ACK" errors
**Fix:** Check wiring, especially MISO (GPIO22) and ground

### STATUS wire byte = 0xFF 0xFF
**Fix:** CS pin not connected or wrong GPIO

### STATUS wire byte = 0x00 0x00
**Fix:** MISO stuck low - check wiring or PN532 power

### Cards not detected
**Fix:** 
- Check antenna connection to PN532
- Try card closer (< 3cm)
- Verify card is ISO14443A type (MIFARE, NTAG)

---

## 🎯 Optional: Hardware Reset (Recommended)

For maximum reliability, connect a reset pin:

1. **Wire:** GPIO24 → PN532 RSTPD_N pin
2. **Edit** `main/config.h`:
   ```c
   #define NFC_RST_PIN     GPIO_NUM_24
   ```
3. **Rebuild** and flash

---

## 📚 More Help

- **Detailed troubleshooting:** See `NFC_TROUBLESHOOTING.md`
- **Build instructions:** See `BUILD_AND_FLASH.md`
- **All changes:** See `CHANGES_SUMMARY.md`

---

## 🧪 Test Procedure

1. **Power on** the ESP32-P4
2. **Wait** for "ready to read cards" message
3. **Hold** NFC card near PN532 antenna (< 3cm)
4. **Observe:**
   - Serial output shows: `NFC card: UID=XXXXXXXX`
   - Green LED lights up
   - Audio beep plays (if access granted)

---

## 💡 Pro Tips

- **Use short wires** (< 20cm) for SPI connections
- **Add 0.1µF capacitor** near PN532 VCC for stability
- **Keep away** from power lines and motors
- **Check logs** every 10s for automatic health checks
- **Telnet access** available on port 23 after Ethernet connects

---

## 🆘 Still Not Working?

1. **Post diagnostic output** - Include full serial log from boot
2. **Check wire bytes** - Look for diagnostic messages in log
3. **Try different card** - Some cards are not ISO14443A
4. **Test with multimeter** - Verify 3.3V/5V on PN532 VCC
5. **Consider clone chip** - Some clones have quirks

**Get the full diagnostic log:**
```bash
idf.py -p /dev/ttyUSB0 monitor | tee nfc_debug.log
```

Then review `nfc_debug.log` for error patterns.
