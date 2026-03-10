# IRQ Implementation - Code Review for Potential Bugs

## Review Date: Mar 5, 2026
## Status: Pre-deployment review

---

## Potential Bug #1: Missing IRQ Configuration in PN532

**Issue:** The PN532 needs to be configured to assert IRQ on card detection.

**Current Code:** We initialize the GPIO IRQ pin, but we don't tell the PN532 to use it.

**Fix Needed:** Add PN532 IRQ configuration command after SAM configuration.

**Severity:** 🔴 CRITICAL - IRQ won't work without this

**Solution:**
```c
// Need to send RFConfiguration command to enable IRQ
// ConfigItem 0x04: IRQ pin configuration
// Byte 1: 0x04 (ConfigItem)
// Byte 2: 0x01 (Enable IRQ on card detection)
```

---

## Potential Bug #2: Race Condition in IRQ Handler

**Issue:** Semaphore might be given multiple times if IRQ fires rapidly.

**Current Code:**
```c
xSemaphoreGiveFromISR(s_irq_sem, &xHigherPriorityTaskWoken);
```

**Risk:** Binary semaphore can only hold one token, so this is actually safe.

**Severity:** 🟢 LOW - Binary semaphore handles this correctly

**Status:** ✅ OK as-is

---

## Potential Bug #3: IRQ Pin Not Cleared After Read

**Issue:** After reading a card, the IRQ pin stays LOW until explicitly cleared.

**Current Code:** We read the card but don't clear the IRQ condition.

**Fix Needed:** Send a command to clear IRQ or read all pending data.

**Severity:** 🟡 MEDIUM - May cause repeated triggers

**Solution:**
```c
// After reading card, clear IRQ by reading all data
// Or send InRelease command to deselect card
```

---

## Potential Bug #4: Missing Timeout in IRQ Wait

**Issue:** If IRQ never fires, we wait forever (well, 100ms).

**Current Code:**
```c
if (pn532_irq_wait_for_card(100)) {  // 100ms timeout
```

**Risk:** 100ms timeout is reasonable, but should match polling mode behavior.

**Severity:** 🟢 LOW - Timeout is present

**Status:** ✅ OK, but could be tuned

---

## Potential Bug #5: IRQ Pin Pull-up Configuration

**Issue:** IRQ pin configured with pull-up, but PN532 might need external pull-up.

**Current Code:**
```c
.pull_up_en    = GPIO_PULLUP_ENABLE,
```

**Risk:** Internal pull-up (~45kΩ) might be too weak for long wires.

**Severity:** 🟡 MEDIUM - May cause false triggers or missed interrupts

**Recommendation:** Add external 10kΩ pull-up resistor on IRQ line.

---

## Potential Bug #6: No Debouncing on IRQ

**Issue:** IRQ might trigger multiple times due to noise or bouncing.

**Current Code:** Direct interrupt on falling edge, no debouncing.

**Risk:** Spurious interrupts could cause false card detections.

**Severity:** 🟡 MEDIUM - Depends on hardware quality

**Mitigation:** Duplicate detection window (1 second) helps, but not perfect.

---

## Potential Bug #7: ISR Service Already Installed

**Issue:** `gpio_install_isr_service()` might fail if already installed by another component.

**Current Code:**
```c
ret = gpio_install_isr_service(0);
if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    // Error
}
```

**Status:** ✅ OK - We handle ESP_ERR_INVALID_STATE (already installed)

---

## Potential Bug #8: Missing PN532 InAutoPoll Command

**Issue:** For true interrupt-driven operation, PN532 should be in AutoPoll mode.

**Current Code:** We use InListPassiveTarget (single poll), not InAutoPoll (continuous).

**Impact:** IRQ won't fire automatically - we still need to send poll commands.

**Severity:** 🔴 CRITICAL - Current implementation won't work as expected

**Fix Needed:** Use InAutoPoll command instead of InListPassiveTarget.

---

## Potential Bug #9: Semaphore Not Initialized Before Use

**Issue:** If IRQ fires before semaphore is created, handler will crash.

**Current Code:** Semaphore created in `pn532_irq_init()`, handler checks for NULL.

**Status:** ✅ OK - NULL check prevents crash

---

## Potential Bug #10: No IRQ Disable on Cleanup

**Issue:** If we need to disable IRQ mode, there's no cleanup function.

**Current Code:** No `pn532_irq_deinit()` function.

**Severity:** 🟡 MEDIUM - Memory leak if re-initializing

**Fix Needed:** Add cleanup function to remove ISR and delete semaphore.

---

## Critical Issues Summary

### 🔴 Must Fix Before Testing:

1. **PN532 IRQ Configuration Missing**
   - PN532 doesn't know to assert IRQ pin
   - Need to send RFConfiguration command

2. **InAutoPoll Not Used**
   - Current code still requires manual polling
   - Need to switch to InAutoPoll mode for true IRQ operation

### 🟡 Should Fix:

3. **IRQ Not Cleared After Read**
   - May cause repeated triggers
   - Need to clear IRQ condition

4. **No External Pull-up Recommendation**
   - Internal pull-up might be weak
   - Document need for external resistor

5. **No Cleanup Function**
   - Can't properly disable IRQ mode
   - Add deinit function

### 🟢 Minor Issues:

6. **No Debouncing**
   - Might get spurious triggers
   - Mitigated by duplicate detection

---

## Recommended Fixes

### Fix #1: Add PN532 IRQ Configuration

Add to `pn532_irq_init()`:

```c
// Configure PN532 to assert IRQ on card detection
uint8_t cmd[3] = { 0x32, 0x04, 0x01 };  // RFConfiguration, IRQ enable
esp_err_t ret = pn532_send_command(cmd, sizeof(cmd));
if (ret != ESP_OK) return ret;
ret = pn532_read_ack();
if (ret != ESP_OK) return ret;
```

### Fix #2: Use InAutoPoll Instead

Replace `pn532_read_passive_target()` with InAutoPoll:

```c
// InAutoPoll: PN532 continuously polls and asserts IRQ when card found
uint8_t cmd[5] = { 0x60, 0x02, 0x01, 0x00, 0x00 };  // InAutoPoll
// 0x60 = InAutoPoll command
// 0x02 = Poll 2 times
// 0x01 = Poll period (150ms)
// 0x00 = Type A (ISO14443A)
// 0x00 = Type B (disabled)
```

### Fix #3: Clear IRQ After Read

Add after reading card:

```c
// Release card to clear IRQ
uint8_t cmd[2] = { 0x52, 0x00 };  // InRelease
pn532_send_command(cmd, sizeof(cmd));
pn532_read_ack();
```

### Fix #4: Add Cleanup Function

```c
void pn532_irq_deinit(void)
{
    if (s_irq_sem != NULL) {
        gpio_isr_handler_remove(NFC_IRQ_PIN);
        vSemaphoreDelete(s_irq_sem);
        s_irq_sem = NULL;
    }
}
```

---

## Testing Checklist

Before enabling IRQ mode (`NFC_USE_IRQ = 1`):

- [ ] Apply Fix #1 (PN532 IRQ configuration)
- [ ] Apply Fix #2 (Use InAutoPoll) OR keep current polling approach
- [ ] Apply Fix #3 (Clear IRQ after read)
- [ ] Add external 10kΩ pull-up on IRQ line
- [ ] Connect GPIO24 → PN532 IRQ pin
- [ ] Test with single card
- [ ] Test with rapid card taps
- [ ] Test with card held for 2+ seconds
- [ ] Monitor for spurious interrupts

---

## Rollback Plan

If IRQ mode has issues:

1. **Immediate:** Set `NFC_USE_IRQ = 0` in config.h
2. **If needed:** Restore backup files
3. **Rebuild and flash**

---

## Recommendation

**DO NOT enable IRQ mode yet!** 

The current implementation is missing critical PN532 configuration. It will compile and run, but IRQ won't actually fire.

**Next Steps:**
1. Apply fixes #1, #2, #3
2. Test in polling mode first (verify no regression)
3. Connect IRQ wire
4. Enable IRQ mode
5. Test thoroughly

---

## Current Status

✅ Code compiles without errors
✅ Polling mode still works (NFC_USE_IRQ = 0)
❌ IRQ mode NOT ready for testing
❌ Missing PN532 IRQ configuration
❌ Missing InAutoPoll implementation

**Recommendation:** Keep `NFC_USE_IRQ = 0` until fixes are applied.
