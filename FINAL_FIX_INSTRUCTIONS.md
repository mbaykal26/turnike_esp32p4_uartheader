# Final Fix for Clone PN532 - MUST REBUILD!

## Critical Changes Made

I've added three key fixes for your clone PN532 chip:

### 1. Dummy Read Before SAM Configuration
Flushes any stale data from the previous command

### 2. Increased Delay Before ACK Read  
Changed from 5ms to 10ms to give clone chip more processing time

### 3. Increased Delay in SAM Configuration
Changed from 10ms to 20ms before reading ACK

## YOU MUST REBUILD - Current Binary is OLD!

Your device is still running firmware from `Mar 4 2026 16:05:15`.
The changes I just made are NOT in that binary.

### Step-by-Step Rebuild Process:

#### Option A: VS Code (Recommended)

1. **Press** `Ctrl+Shift+P`
2. **Type**: `ESP-IDF: Full Clean`
3. **Press Enter** and wait for "Clean complete"
4. **Press** `Ctrl+E` then `B` (Build)
5. **Wait** for "Build complete" 
6. **Press** `Ctrl+E` then `F` (Flash)
7. **Press** `Ctrl+E` then `M` (Monitor)

#### Option B: Command Line

```bash
# In your project directory
idf.py fullclean
idf.py build
idf.py -p COM5 flash monitor
```

## How to Verify You Have the New Firmware

After flashing, check the serial output for:

1. **New build timestamp** - Should show current time, NOT `16:05:15`
2. **New diagnostic message** - Should see:
   ```
   [pn532]   Attempting SAM configuration...
   ```
3. **Success message** - Should see:
   ```
   [pn532] PN532 SAM configured, MaxRetries=5 — ready to read cards
   [main] NFC: READY   Audio: ES8311 I2S Speaker
   ```

## If You're Not Sure How to Build

### Check if ESP-IDF Extension is Installed:

1. Open VS Code
2. Look at the bottom status bar
3. You should see "ESP-IDF" with version number
4. If not, install it:
   - Press `Ctrl+Shift+X` (Extensions)
   - Search "ESP-IDF"
   - Install "Espressif IDF"
   - Follow setup wizard

### Alternative: Use idf.py Command

If you have ESP-IDF installed via command line:

```bash
# Windows (Git Bash or similar)
cd /c/path/to/your/project
export IDF_PATH=~/esp/esp-idf
. $IDF_PATH/export.sh
idf.py fullclean build flash monitor

# Or if you have ESP-IDF in different location
# Find it first:
where idf.py
# Then use that path
```

## What These Changes Do

Your clone PN532 has a "sticky STATUS byte" problem:
- STATUS stays at 0x01 (ready) even after commands complete
- This causes the code to read ACK/response too early
- We get the previous command's response instead of the current ACK

The fixes:
1. **Dummy read** - Clears any leftover data
2. **Longer delays** - Ensures PN532 has time to process and generate ACK
3. **Minimum poll count** - Waits for at least 3 STATUS polls before accepting ready

## Expected Result

After rebuilding and flashing, you should see:

```
[pn532] PN532: FW=1.6  IC=0x32  Support=0x07
[pn532]   Attempting SAM configuration...
[pn532] PN532 SAM configured, MaxRetries=5 — ready to read cards
[main] NFC: READY   Audio: ES8311 I2S Speaker
```

Then test with an NFC card - it should work!

## Still Getting "Bad ACK"?

If you STILL get "Bad ACK" after rebuilding with the new code, we can:

1. Reduce SPI speed to 500 kHz
2. Increase delays even more (50ms+)
3. Try a different approach (polling-based instead of interrupt)

But first, **you must rebuild** to get the new code!
