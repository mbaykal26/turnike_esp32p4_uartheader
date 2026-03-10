# Force Clean Rebuild

## The Problem
Your device is still running the old firmware. The build system didn't detect the changes.

## Solution: Force Clean Rebuild

### Using VS Code ESP-IDF Extension:

1. **Open Command Palette**: `Ctrl+Shift+P`
2. **Type**: "ESP-IDF: Full Clean"
3. **Press Enter**
4. **Then build**: `Ctrl+E` → `B`
5. **Flash**: `Ctrl+E` → `F`
6. **Monitor**: `Ctrl+E` → `M`

### Using Command Line:

```bash
# Navigate to project directory
cd /path/to/your/project

# Full clean
idf.py fullclean

# Rebuild
idf.py build

# Flash and monitor
idf.py -p COM5 flash monitor
```

## Verify New Build

After flashing, check the build timestamp in the serial output. It should show today's date and a time AFTER you ran the build command.

Look for:
```
I (XXX) main: Build: Mar 4 2026 [NEW_TIME]
```

The time should be different from `16:05:15`.

## What Should Change

After the new build, you should see:
```
[pn532]   Attempting SAM configuration...
[pn532]   [diag#01] TX wire:
```

This is the new diagnostic output that wasn't in the previous version.
