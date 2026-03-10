# Quick Build & Flash Guide

## Prerequisites

### Install ESP-IDF Extension (VS Code)
1. Open VS Code
2. Go to Extensions (Ctrl+Shift+X)
3. Search for "ESP-IDF"
4. Install "Espressif IDF" extension
5. Follow the setup wizard to install ESP-IDF framework

### Or Install ESP-IDF Manually
```bash
# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3  # or latest stable version

# Install
./install.sh esp32p4

# Set up environment (add to ~/.bashrc for permanent)
. $HOME/esp/esp-idf/export.sh
```

## Building the Project

### Method 1: VS Code (Recommended)
1. Open this project folder in VS Code
2. Press `Ctrl+E` then `D` → Select "esp32p4" as target
3. Press `Ctrl+E` then `B` → Build project
4. Press `Ctrl+E` then `F` → Flash to device
5. Press `Ctrl+E` then `M` → Open serial monitor

### Method 2: Command Line
```bash
# Navigate to project directory
cd /path/to/esp32p4_access_control

# Set up ESP-IDF environment (if not already done)
. $HOME/esp/esp-idf/export.sh

# Set target (first time only)
idf.py set-target esp32p4

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Or combine flash + monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

### Before Building
1. Copy `main/secrets.h.template` to `main/secrets.h`
2. Edit `main/secrets.h` and add your API bearer token:
   ```c
   #define API_BEARER_TOKEN "your-token-here"
   ```

### Adjust Settings (Optional)
Edit `main/config.h` to change:
- Pin assignments
- SPI frequency
- Timing parameters
- API endpoint

## Serial Monitor

### Exit Monitor
- Press `Ctrl+]` to exit idf.py monitor
- Or `Ctrl+T` then `Ctrl+X` for alternative method

### Useful Monitor Commands
While in monitor, press `Ctrl+T` then:
- `Ctrl+R` - Reset the chip
- `Ctrl+H` - Show help
- `Ctrl+X` - Exit monitor

## Troubleshooting Build Issues

### "idf.py: command not found"
```bash
# Source the ESP-IDF environment
. $HOME/esp/esp-idf/export.sh
```

### "Target 'esp32p4' not found"
```bash
# Make sure you have ESP-IDF v5.1 or later
cd ~/esp/esp-idf
git fetch
git checkout v5.3
git submodule update --init --recursive
./install.sh esp32p4
```

### "Port /dev/ttyUSB0 not found"
```bash
# List available ports
ls /dev/tty*

# On Linux, you might need permissions
sudo usermod -a -G dialout $USER
# Then log out and back in

# On Windows, use COM port
idf.py -p COM3 flash monitor
```

### Build Errors
```bash
# Clean build
idf.py fullclean

# Rebuild
idf.py build
```

## Monitoring NFC Reader

After flashing, watch the serial output for:

```
[pn532] PN532 SPI init: SCK=20 MISO=22 MOSI=21 CS=23  1000 kHz
[pn532]   Waiting 1000 ms for PN532 power-on / stabilisation...
[pn532]   Sending PN532 SPI wake preamble (0x55 x5)...
[pn532]   Diagnostic mode ON — logging raw SPI wire bytes
[pn532] PN532: FW=1.6  IC=0x32  Support=0x01
[pn532] PN532 SAM configured, MaxRetries=5 — ready to read cards
```

If you see "PN532 not responding", refer to `NFC_TROUBLESHOOTING.md`.

## Quick Test

1. Flash the firmware
2. Open serial monitor
3. Wait for "ready to read cards" message
4. Hold an NFC card near the PN532 antenna
5. You should see: `NFC card: UID=XXXXXXXX  SAK=0xXX  ATQA=0xXXXX`

## Network Access

The device will:
1. Connect via Ethernet (IP101 PHY)
2. Get IP via DHCP
3. Start Telnet server on port 23
4. Display IP address in serial output

Connect via Telnet:
```bash
telnet <device-ip-address>
```

You'll see the same logs as serial monitor, plus status heartbeats every 30 seconds.
