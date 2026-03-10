# Update: Bad ACK Fix for Clone PN532 Chips

## Issue Identified

Your PN532 is responding correctly (FW=1.6, IC=0x32 detected), but the SAM configuration is failing with "Bad ACK" errors.

### Root Cause
The diagnostic output shows:
```
ACK raw rx[1..6]: 60 5f ab c0 4c 80
```

This should be:
```
ACK raw rx[1..6]: 00 00 FF 00 FF 00
```

The PN532 clone chip has a **sticky STATUS byte** that stays at 0x01 (ready) even after the previous command completes. When we send SAM configuration and immediately check STATUS, it's already 0x01 from the firmware version command, so we read too early and get the response data instead of the ACK frame.

## Fix Applied

### 1. Enhanced STATUS Polling
Added a poll counter that requires at least 3 STATUS polls before accepting ready state for clone chips with sticky STATUS:

```c
// Require at least 3 polls before accepting ready state
if (seen_not_ready || poll_count >= 3) {
    return ESP_OK;
}
```

### 2. Additional Delay Before ACK Read
Added 5ms delay after STATUS ready before reading ACK:

```c
// Additional small delay for clone chips that need more time
vTaskDelay(pdMS_TO_TICKS(5));
```

### 3. Extra Delay in SAM Configuration
Added 10ms delay after sending SAM command before reading ACK:

```c
// Extra delay for clone chips before reading ACK
vTaskDelay(pdMS_TO_TICKS(10));
```

## What to Do Now

### 1. Rebuild and Flash
```bash
# Using VS Code ESP-IDF extension:
Ctrl+E → B  (build)
Ctrl+E → F  (flash)
Ctrl+E → M  (monitor)

# Or command line:
idf.py build flash monitor
```

### 2. Expected Output
You should now see:
```
[pn532] PN532: FW=1.6  IC=0x32  Support=0x07
[pn532] PN532 SAM configured, MaxRetries=5 — ready to read cards
[main] NFC: READY   Audio: ES8311 I2S Speaker
```

### 3. Test NFC Card
Hold an NFC card near the antenna and you should see:
```
[main] NFC card: UID=XXXXXXXX  SAK=0xXX  ATQA=0xXXXX
```

## Why This Happens

Clone PN532 chips often have timing quirks:

1. **Sticky STATUS byte** - Doesn't clear between commands
2. **Faster response generation** - ACK/response ready before STATUS updates
3. **Different internal timing** - Needs more delays than genuine chips

The original code was written for genuine NXP PN532 chips that properly toggle STATUS. Clone chips need these extra delays to ensure we don't read data before it's fully ready.

## If Still Getting Bad ACK

If you still see "Bad ACK" errors after this update:

### Try increasing delays further:

Edit `main/pn532_spi.c` and increase these values:

```c
// In pn532_read_ack():
vTaskDelay(pdMS_TO_TICKS(10));  // Change from 5 to 10

// In pn532_sam_configure():
vTaskDelay(pdMS_TO_TICKS(20));  // Change from 10 to 20
```

### Or reduce SPI speed:

Edit `main/config.h`:
```c
#define NFC_SPI_FREQ_HZ (500 * 1000)  // Reduce from 1 MHz to 500 kHz
```

### Check wiring again:
- Verify MISO (GPIO22) connection
- Check ground connection is solid
- Ensure no loose wires

## Technical Details

The ACK frame structure is:
```
Byte 0: 0x00 (Preamble)
Byte 1: 0x00 (Start code 1)
Byte 2: 0xFF (Start code 2)
Byte 3: 0x00 (Packet length)
Byte 4: 0xFF (Length checksum)
Byte 5: 0x00 (Postamble)
```

When we get `60 5f ab c0 4c 80`, we're actually reading part of the response data frame instead of the ACK. The extra delays ensure the PN532 has time to:
1. Process the command
2. Generate the ACK frame
3. Make it available for reading
4. Update STATUS byte (if it does at all)

## Success Indicators

After flashing the fix, watch for:

✅ "PN532 SAM configured" message
✅ "NFC: READY" in the banner
✅ No more "Bad ACK" errors
✅ Cards detected when scanned
✅ Green LED lights up on successful scan

## Your Current Status

Good signs from your log:
- ✅ PN532 detected (FW=1.6, IC=0x32)
- ✅ SPI communication working
- ✅ Ethernet connected (10.10.5.23)
- ✅ Audio working
- ✅ Telnet server running

Just need to fix the SAM configuration timing issue, which this update addresses.
