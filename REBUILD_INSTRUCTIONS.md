# Rebuild Required - Diagnostic Enhancement

## What Changed

I've enabled diagnostic logging for the SAM configuration step so we can see exactly what's happening when the "Bad ACK" error occurs.

## How to Rebuild

### Option 1: VS Code ESP-IDF Extension
```
Ctrl+E → B  (build)
Ctrl+E → F  (flash)
Ctrl+E → M  (monitor)
```

### Option 2: Command Line
```bash
idf.py build flash monitor
```

## What to Look For

After flashing, you should see additional diagnostic output like:

```
[pn532]   Attempting SAM configuration...
[pn532]   [diag#XX] TX wire:
[pn532] 80 00 00 ff 28 ...  (SAM config command)
[pn532]   [diag#XX] TX wire:
[pn532] 40 00  (STATUS poll)
[pn532]   [diag#XX] RX wire:
[pn532] XX XX  (STATUS response)
[pn532]   [diag#XX] TX wire:
[pn532] c0 00 00 00 00 00 00  (ACK read)
[pn532]   [diag#XX] RX wire:
[pn532] XX XX XX XX XX XX XX  (should be: 80 00 00 ff 00 ff 00)
```

This will show us exactly what bytes are being exchanged during SAM configuration and help identify the timing issue.

## Expected Outcome

With the timing fixes already in place (10ms delay before ACK read, minimum 3 STATUS polls), the SAM configuration should now succeed. The diagnostic output will confirm this.

## If It Still Fails

If you still see "Bad ACK" after this rebuild, post the complete diagnostic output and we'll adjust the timing delays further based on what we see.
