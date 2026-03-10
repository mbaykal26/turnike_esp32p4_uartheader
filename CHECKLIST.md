# Quick Checklist - Fix NFC Reader

## ☐ Step 1: Clean Build
- [ ] Open VS Code
- [ ] Press `Ctrl+Shift+P`
- [ ] Type "ESP-IDF: Full Clean"
- [ ] Press Enter
- [ ] Wait for "Clean complete"

## ☐ Step 2: Build
- [ ] Press `Ctrl+E` then `B`
- [ ] Wait for "Build complete" (may take 1-2 minutes)
- [ ] Check for any errors (should be none)

## ☐ Step 3: Flash
- [ ] Connect ESP32-P4 via USB
- [ ] Press `Ctrl+E` then `F`
- [ ] Wait for "Flash complete"

## ☐ Step 4: Monitor
- [ ] Press `Ctrl+E` then `M`
- [ ] Watch serial output

## ☐ Step 5: Verify New Firmware
Look for these in serial output:
- [ ] Build timestamp is NEW (not 16:05:15)
- [ ] See "Attempting SAM configuration..."
- [ ] See "PN532 SAM configured"
- [ ] See "NFC: READY"

## ☐ Step 6: Test NFC Card
- [ ] Hold NFC card near PN532 antenna
- [ ] Should see "NFC card: UID=..."
- [ ] Green LED lights up

---

## If Build Fails

### "idf.py not found" or "ESP-IDF not configured"

Install ESP-IDF extension:
1. Press `Ctrl+Shift+X`
2. Search "ESP-IDF"
3. Install "Espressif IDF"
4. Follow setup wizard
5. Restart VS Code
6. Try again from Step 1

### "Port not found" or "Cannot open COM5"

1. Check USB cable is connected
2. Check Device Manager (Windows) for COM port number
3. Update port in VS Code:
   - Press `Ctrl+E` then `P`
   - Select correct COM port

### Build errors about missing files

1. Make sure you're in the project directory
2. Check that all files are present:
   - `CMakeLists.txt`
   - `main/` folder
   - `main/CMakeLists.txt`

---

## Alternative: Command Line Build

If VS Code isn't working, use command line:

```bash
# Navigate to project
cd /path/to/esp32p4_access_control

# Clean
idf.py fullclean

# Build
idf.py build

# Flash (replace COM5 with your port)
idf.py -p COM5 flash

# Monitor
idf.py -p COM5 monitor
```

---

## Success Criteria

✅ New build timestamp in serial output
✅ "PN532 SAM configured" message
✅ "NFC: READY" in banner
✅ NFC cards detected when scanned
✅ No "Bad ACK" errors

---

## Need Help?

Post the complete serial output showing:
1. Build timestamp
2. PN532 initialization messages
3. Any error messages

Include the output from boot until you see "Entering main access-control loop".
