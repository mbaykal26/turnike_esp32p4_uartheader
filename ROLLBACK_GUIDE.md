# Easy Rollback Guide - IRQ Experiment

## Backup Files Created ✅

The following stable working files have been backed up:

```
main/app_main.c.STABLE_BACKUP
main/pn532_spi.c.STABLE_BACKUP
main/pn532_spi.h.STABLE_BACKUP
main/config.h.STABLE_BACKUP
```

These are your **TESTED AND WORKING** files from before the IRQ experiment.

## Quick Rollback Methods

### Method 1: Simple Config Change (Recommended)

If IRQ mode has issues, just change one line in `main/config.h`:

```c
#define NFC_USE_IRQ     0   // Change from 1 to 0
```

Then rebuild and flash. The code will automatically use stable polling mode.

**Time:** 2 minutes (just rebuild and flash)

### Method 2: Full File Restore (If Needed)

If something goes wrong with the code itself, restore all backup files:

#### Windows PowerShell:
```powershell
Copy-Item main/app_main.c.STABLE_BACKUP main/app_main.c -Force
Copy-Item main/pn532_spi.c.STABLE_BACKUP main/pn532_spi.c -Force
Copy-Item main/pn532_spi.h.STABLE_BACKUP main/pn532_spi.h -Force
Copy-Item main/config.h.STABLE_BACKUP main/config.h -Force
```

#### Linux/Mac:
```bash
cp main/app_main.c.STABLE_BACKUP main/app_main.c
cp main/pn532_spi.c.STABLE_BACKUP main/pn532_spi.c
cp main/pn532_spi.h.STABLE_BACKUP main/pn532_spi.h
cp main/config.h.STABLE_BACKUP main/config.h
```

Then rebuild and flash.

**Time:** 3 minutes (restore + rebuild + flash)

## Testing Plan

### Before Enabling IRQ Mode

1. ✅ Current system is working (polling mode)
2. ✅ Backups created
3. ⚠️ Need to connect hardware: GPIO24 → PN532 IRQ pin

### Enable IRQ Mode

1. Connect wire: ESP32 GPIO24 → PN532 IRQ pin
2. Edit `main/config.h`:
   ```c
   #define NFC_USE_IRQ     1   // Change from 0 to 1
   ```
3. Rebuild and flash
4. Test card detection

### If IRQ Works ✅

- Instant card detection (no 30ms polling delay)
- Lower CPU usage
- More responsive system
- Keep IRQ mode enabled

### If IRQ Has Issues ❌

**Symptoms to watch for:**
- Cards not detected
- System crashes
- Spurious interrupts
- Slower than polling mode

**Quick fix:**
1. Change `NFC_USE_IRQ` back to 0
2. Rebuild and flash
3. System returns to stable polling mode

## Current Status

```
Mode:           POLLING (stable)
NFC_USE_IRQ:    0
Backups:        ✅ Created
Hardware:       No IRQ wire needed yet
Status:         READY FOR EXPERIMENT
```

## IRQ Mode Requirements

### Hardware Connection Needed:
```
ESP32-P4 GPIO24 ──→ PN532 IRQ pin
```

### How IRQ Works:
1. PN532 pulls IRQ pin LOW when card detected
2. ESP32 gets instant interrupt (no polling)
3. Read card data immediately
4. Much faster and more efficient

### Benefits:
- ⚡ Instant detection (0ms delay vs 30ms polling)
- 💪 Lower CPU usage (no continuous polling)
- 🎯 More responsive system

### Risks:
- ⚠️ Needs hardware modification (one wire)
- ⚠️ Untested with your clone chip
- ⚠️ May need timing adjustments

## Rollback Decision Tree

```
Is NFC working?
├─ YES → Keep current configuration
└─ NO
   ├─ Just enabled IRQ mode?
   │  └─ Change NFC_USE_IRQ to 0, rebuild
   │
   └─ Code changes made?
      └─ Restore backup files, rebuild
```

## Safety Features

1. **Config flag:** Easy toggle between modes
2. **Backup files:** Complete restore available
3. **No data loss:** All changes are code-only
4. **Quick rollback:** 2-3 minutes max

## Files That Will Change

When implementing IRQ mode, these files will be modified:

- `main/config.h` - Add IRQ pin and mode flag ✅ (already done)
- `main/pn532_spi.h` - Add IRQ functions (will do)
- `main/pn532_spi.c` - Add IRQ handler (will do)
- `main/app_main.c` - Use IRQ instead of polling (will do)

All changes will be wrapped in `#if NFC_USE_IRQ` blocks, so you can switch modes easily.

## Recommendation

**Start with:** `NFC_USE_IRQ = 0` (polling mode - stable)

**Test first:** Make sure current system still works after adding IRQ code

**Then try:** `NFC_USE_IRQ = 1` (IRQ mode - experimental)

**If issues:** Change back to 0 immediately

## Summary

✅ **Easy rollback:** Just change one number (0 or 1)
✅ **Safe:** Full backups available
✅ **Fast:** 2-3 minutes to rollback
✅ **No risk:** Can always go back to working state

You're ready to experiment safely!
