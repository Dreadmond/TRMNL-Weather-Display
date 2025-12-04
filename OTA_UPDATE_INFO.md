# OTA Auto-Update Implementation

## Overview

The device now automatically checks for firmware updates from your GitHub repository and installs them when available.

## How It Works

1. **Update Check Frequency**: Every 12 wake cycles (approximately 6 hours at 30-minute intervals)
2. **GitHub Repository**: `Dreadmond/TRMNL-Weather-Display`
3. **Release Format**: Uses the latest GitHub release with a `.bin` file attached

## Files Created

- `include/ota_update.h` - OTA update function declarations
- `src/ota_update.cpp` - OTA update implementation
- Integrated into `src/main.cpp` - Called after WiFi connection

## Configuration

The OTA system checks:
- GitHub releases API for latest version
- Downloads `firmware.bin` from release assets
- Compares version strings (e.g., "1.0.0" vs "1.0.1")
- Only updates if newer version detected

## To Release a New Firmware Version

1. Update firmware version in `src/ota_update.cpp`:
   ```cpp
   #define FIRMWARE_VERSION "1.0.1"  // Increment this
   ```

2. Build the firmware:
   ```bash
   pio run
   ```

3. Find the compiled firmware:
   ```
   .pio/build/trmnl_og/firmware.bin
   ```

4. Create a GitHub release:
   - Go to: https://github.com/Dreadmond/TRMNL-Weather-Display/releases/new
   - Tag version: `v1.0.1` (must match FIRMWARE_VERSION)
   - Release title: `Version 1.0.1`
   - Upload `firmware.bin` as a release asset

5. The device will automatically update on the next check cycle (within 6 hours)

## Customization

### Change Update Check Interval
Edit `src/ota_update.cpp`:
```cpp
#define OTA_CHECK_INTERVAL 12  // Change this number (12 = every 12 wake cycles)
```

### Change GitHub Repository
Edit `src/ota_update.cpp`:
```cpp
#define GITHUB_REPO_OWNER "Dreadmond"
#define GITHUB_REPO_NAME "TRMNL-Weather-Display"
```

### Disable OTA Updates
Comment out the OTA check in `src/main.cpp` around line 813:
```cpp
// if (checkAndPerformOTAUpdate()) {
//   return;
// }
```

## Notes

- Updates only happen if battery is above low battery threshold
- Device will restart automatically after successful update
- Update check is skipped if already checked the same version
- All update attempts are logged to serial output

