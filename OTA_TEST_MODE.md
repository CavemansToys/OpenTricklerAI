# OTA Test Mode - Bare Board Testing Guide

## Overview

Test the OTA firmware update system on a **bare Pico W board** with **no external hardware** connected.

This test mode disables all hardware peripherals (motors, display, scale, servos, etc.) and only enables:
- ✅ WiFi connectivity
- ✅ OTA firmware update system
- ✅ REST API endpoints
- ✅ Watchdog timer
- ✅ Serial console output

Perfect for validating the OTA system before deploying to production hardware.

---

## Hardware Requirements

### Minimal Setup:
- **Raspberry Pi Pico W** or **Pico 2 W** board
- **USB cable** (for power and serial console)
- **WiFi network** with internet access (for testing URL downloads)

### That's it! No other hardware needed.

---

## Prerequisites

- CMake 3.25+
- ARM GCC toolchain
- Pico SDK (included as submodule)
- Python 3.x
- Serial terminal (screen, minicom, PuTTY, etc.)

---

## Build Instructions

### Step 1: Create Test Build Directory

```bash
cd OpenTrickler-RP2040-Controller-main

# Create separate test build folder
mkdir build_test
cd build_test
```

### Step 2: Configure with OTA Test Mode

```bash
# For Pico W (RP2040)
cmake .. \
  -DBUILD_OTA_BOOTLOADER=ON \
  -DUSE_OTA_LINKER_SCRIPT=ON \
  -DOTA_TEST_MODE=ON \
  -DPICO_BOARD=pico_w

# OR for Pico 2 W (RP2350)
cmake .. \
  -DBUILD_OTA_BOOTLOADER=ON \
  -DUSE_OTA_LINKER_SCRIPT=ON \
  -DOTA_TEST_MODE=ON \
  -DPICO_BOARD=pico2_w
```

### Step 3: Build

```bash
make -j4
```

### Step 4: Verify Test Mode

You should see in the CMake output:
```
==============================================
OTA Firmware Update Configuration
==============================================
BUILD_OTA_BOOTLOADER: ON
USE_OTA_LINKER_SCRIPT: ON
OTA_TEST_MODE: ON
-- OTA Test Mode ENABLED - Hardware peripherals will be disabled
==============================================
```

---

## WiFi Configuration

### Option 1: Environment Variables (Recommended for Testing)

Create `.env` file in the root directory:
```bash
WIFI_SSID=YourNetworkName
WIFI_PASSWORD=YourPassword
```

### Option 2: Code Modification

Edit `src/wireless.cpp` and hardcode credentials temporarily:
```c
const char* ssid = "YourNetworkName";
const char* password = "YourPassword";
```

---

## Initial Firmware Installation

### Method 1: Flash via USB (First Time)

1. **Hold BOOTSEL button** on Pico W
2. **Connect USB cable**
3. Device appears as **RPI-RP2** drive
4. **Copy `app.uf2`** to the drive:
   ```bash
   cp app.uf2 /media/RPI-RP2/
   # Or drag-and-drop in file explorer
   ```
5. Board will automatically reboot

### Method 2: Using picotool

```bash
# List devices
picotool info

# Flash firmware
picotool load app.uf2 -f

# Reboot
picotool reboot
```

---

## Testing the OTA System

### Connect to Serial Console

Monitor boot messages and system status:

```bash
# Linux/Mac
screen /dev/ttyACM0 115200

# Or using minicom
minicom -D /dev/ttyACM0 -b 115200

# Windows (PowerShell)
# Use PuTTY or Windows Terminal with COM port
```

### Expected Boot Output

```
==============================================
OTA TEST MODE - Bare Board Testing
==============================================
Hardware peripherals disabled for testing
Only WiFi and OTA system active

==============================================
OTA Firmware Update System
==============================================
Running from: Bank A
Version: 1.0.0
Size: 524288 bytes
CRC32: 0xABCD1234
Confirming successful boot...
Boot confirmed - boot counter reset
==============================================

Starting OTA test task...
OTA Test Task started
Access the device at: http://opentrickler.local
REST API endpoints available:
  GET  /rest/firmware_status
  POST /upload (with firmware binary)
  GET  /rest/firmware_download?url=<url>
  POST /rest/firmware_activate
  POST /rest/firmware_rollback
  POST /rest/firmware_cancel

[0] OTA system running, waiting for commands...
Current bank: A
```

---

## Test Scenarios

### Test 1: Check Firmware Status

```bash
curl http://opentrickler.local/rest/firmware_status | jq
```

**Expected Response:**
```json
{
  "current_bank": "A",
  "bank_a": {
    "valid": true,
    "size": 524288,
    "crc32": "0xABCD1234",
    "version": "1.0.0",
    "boot_count": 0
  },
  "bank_b": {
    "valid": false,
    "size": 0,
    "crc32": "0x00000000",
    "version": "",
    "boot_count": 0
  },
  "update_status": {
    "state": "idle",
    "progress": 0,
    "target_bank": "none"
  },
  "rollback_occurred": false
}
```

### Test 2: Upload New Firmware via HTTP POST

```bash
# Make a small change to the code (e.g., change version string)
# Rebuild in the test folder
cd build_test
make

# Calculate CRC32
crc32=$(crc32 app.bin)

# Upload firmware
curl -X POST http://opentrickler.local/upload \
  -H "X-Firmware-Size: $(stat -f%z app.bin)" \
  -H "X-Firmware-CRC32: 0x${crc32}" \
  -H "X-Firmware-Version: 1.0.1-test" \
  --data-binary @app.bin

# Monitor progress
curl http://opentrickler.local/rest/firmware_status | jq .update_status
```

**Expected:**
- Status changes: `idle` → `preparing` → `erasing` → `receiving` → `validating` → `complete`
- Watch serial console for progress messages

### Test 3: Activate New Firmware

```bash
curl -X POST http://opentrickler.local/rest/firmware_activate
```

**Expected:**
- Device reboots immediately
- Serial console shows boot from Bank B
- New version appears in status

### Test 4: Download Firmware from URL

Host firmware on a local web server:
```bash
# Start simple HTTP server in build_test directory
python3 -m http.server 8000

# In another terminal, trigger download
curl "http://opentrickler.local/rest/firmware_download?url=http://YOUR_IP:8000/app.bin&version=1.0.2-url"

# Monitor progress
watch -n 1 'curl -s http://opentrickler.local/rest/firmware_status | jq .update_status'
```

### Test 5: Test Automatic Rollback

Create intentionally broken firmware:

1. Edit `src/app.cpp` and comment out boot confirmation:
   ```cpp
   // firmware_manager_confirm_boot();  // COMMENTED OUT
   ```

2. Rebuild and upload:
   ```bash
   make
   curl -X POST http://opentrickler.local/upload \
     -H "X-Firmware-Size: $(stat -c%s app.bin)" \
     -H "X-Firmware-CRC32: 0x$(crc32 app.bin)" \
     --data-binary @app.bin
   curl -X POST http://opentrickler.local/rest/firmware_activate
   ```

3. **Watch serial console:**
   - Boots into new firmware
   - Doesn't confirm boot
   - Reboots automatically (watchdog or manual)
   - Repeats 3 times
   - **Automatically rolls back to previous working firmware**

4. **Verify rollback:**
   ```bash
   curl http://opentrickler.local/rest/firmware_status | jq .rollback_occurred
   # Should return: true
   ```

### Test 6: Manual Rollback

```bash
curl -X POST http://opentrickler.local/rest/firmware_rollback
```

**Expected:**
- Device switches to opposite bank
- Reboots immediately

### Test 7: Cancel Update in Progress

```bash
# Start a download
curl "http://opentrickler.local/rest/firmware_download?url=http://example.com/large_file.bin"

# Cancel it while in progress
curl -X POST http://opentrickler.local/rest/firmware_cancel

# Verify cancelled
curl http://opentrickler.local/rest/firmware_status | jq .update_status.state
# Should return: "idle"
```

---

## Power Loss Testing

### Test Update Resilience

1. Start firmware upload:
   ```bash
   curl -X POST http://opentrickler.local/upload \
     -H "X-Firmware-Size: $(stat -c%s app.bin)" \
     --data-binary @app.bin
   ```

2. **Unplug USB cable** during upload (simulating power loss)

3. **Reconnect power**

4. **Verify system boots with old firmware:**
   ```bash
   curl http://opentrickler.local/rest/firmware_status
   ```

**Expected:**
- Old firmware still valid and boots
- Update status shows error or incomplete state
- Can retry update successfully

---

## Troubleshooting

### Board Not Connecting to WiFi

**Check Serial Console:**
```
WiFi connecting...
Failed to connect: <error>
```

**Solutions:**
1. Verify SSID and password
2. Check 2.4GHz WiFi (Pico W doesn't support 5GHz)
3. Move closer to router
4. Check router allows new devices

### Can't Access http://opentrickler.local

**mDNS might not work on your network.**

**Find IP Address from Serial Console:**
```
WiFi connected!
IP Address: 192.168.1.123
```

**Use IP directly:**
```bash
curl http://192.168.1.123/rest/firmware_status
```

### Upload Fails with "CRC32 Mismatch"

**Calculate correct CRC32:**

```bash
# Linux/Mac
crc32 app.bin

# Or use Python
python3 -c "import zlib; print(hex(zlib.crc32(open('app.bin','rb').read())))"
```

### Serial Console Shows Gibberish

**Check baud rate:** Must be **115200**

### System Keeps Rebooting

**Watchdog timeout** - OTA test task should be feeding watchdog every second.

**Check:**
- Task stack size sufficient (2048 bytes)
- No infinite loops blocking task
- vTaskDelay called regularly

---

## Differences from Production Build

| Feature | Test Mode | Production Mode |
|---------|-----------|-----------------|
| Hardware Init | Disabled | Enabled |
| Display | None | Mini 12864 |
| Motors | None | TMC Steppers |
| Scale | None | UART Scale |
| Menu Task | OTA Test Task | Full Menu |
| Serial Output | Verbose | Normal |
| Purpose | OTA Testing | Full Controller |

---

## Next Steps After Successful Testing

1. **Build production firmware:**
   ```bash
   mkdir build_production
   cd build_production
   cmake .. -DBUILD_OTA_BOOTLOADER=ON -DUSE_OTA_LINKER_SCRIPT=ON -DOTA_TEST_MODE=OFF
   make
   ```

2. **Deploy to hardware** with all peripherals connected

3. **Test OTA updates** on production hardware

4. **Add authentication** for security (see `OTA_BUILD_AND_USAGE.md`)

5. **Implement firmware signing** for production deployment

---

## Safety Notes

- ✅ **Test mode is safe** - No hardware can be damaged
- ✅ **Brick-proof** - Always have working firmware
- ✅ **Automatic rollback** - Bad firmware reverts automatically
- ⚠️ **No authentication** - Only use on trusted networks
- ⚠️ **USB power only** - Don't connect external power without proper circuit

---

## Summary

You now have a **complete OTA testing environment** that:
- ✅ Runs on bare Pico W board
- ✅ Tests all OTA features
- ✅ Validates firmware updates
- ✅ Demonstrates automatic rollback
- ✅ Provides comprehensive debugging

Perfect for development, testing, and demonstrations!

---

**Last Updated:** 2026-01-08
**Test Mode Version:** 1.0.0
**Status:** Ready for Testing ✅
