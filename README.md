# TRMNL Weather Station

A beautiful weather display for the TRMNL OG e-paper device, inspired by [LMARZEN's esp32-weather-epd](https://github.com/lmarzen/esp32-weather-epd).

## Features

- **Weather Data**: Current conditions, hourly forecast, and 5-day forecast from OpenWeatherMap
- **Indoor Readings**: Temperature and humidity from your Home Assistant Nest thermostat
- **Battery Monitoring**: Accurate battery level estimation with low-battery protection
- **MQTT Telemetry**: Automatic Home Assistant integration with device auto-discovery
- **Button Support**: Wake-up on button press, long-press to reset WiFi
- **Deep Sleep**: Efficient power management with configurable refresh intervals
- **WiFi Captive Portal**: Easy setup without hardcoding credentials
- **OTA Auto-Update**: Automatic firmware updates from GitHub releases
- **Watchdog Timer**: Prevents device freezes by forcing sleep after 30 seconds

## Hardware

- TRMNL OG (ESP32-C3 with 7.5" 800x480 e-paper display)
- Built-in LiPo battery support

## Configuration

All settings are in `include/config.h`:

### Already Configured For You:
- **Location**: Stretford, Manchester (M32 8WF)
- **OpenWeatherMap API**: Your API key
- **Home Assistant**: 
  - Host: 10.0.1.24
  - Temperature: sensor.nest_thermostat_temperature
  - Humidity: sensor.nest_thermostat_humidity
- **MQTT**: Port 1883, user matt, pass honey1
- **Units**: Celsius, mph, 24-hour time
- **Timezone**: UK (GMT/BST)

## Flashing Instructions

### Method 1: Using PlatformIO IDE
1. Open this project in VS Code with PlatformIO extension
2. Connect TRMNL OG via USB-C (use a data cable)
3. Put device in flash mode:
   - Turn OFF the device
   - Hold the BOOT button (circular button on back)
   - Turn ON the device while holding the button
   - Release the button
4. Click "Upload" in PlatformIO

### Method 2: Using Command Line
```bash
cd "/Users/matt/Documents/PlatformIO/Projects/TRMNL Weather Station"
pio run -t upload
```

## First Boot Setup

1. After flashing, the device will start in WiFi setup mode
2. Connect your phone/computer to the WiFi network: **TRMNL-Weather**
3. Open http://192.168.4.1 in your browser
4. Select your home WiFi network and enter the password
5. The device will restart and begin displaying weather

## Button Functions

| Action | Duration | Function |
|--------|----------|----------|
| Short press | < 1 sec | Immediate refresh |
| Medium press | 1-5 sec | Immediate refresh |
| Long press | 5-15 sec | Reset WiFi credentials |
| Very long press | 15+ sec | Factory reset |

## Home Assistant Integration

The device automatically registers itself with Home Assistant via MQTT discovery.

### Sensors Created:
- **Battery** - Battery percentage (0-100%)
- **Battery Voltage** - Battery voltage in volts
- **WiFi Signal** - WiFi RSSI in dBm
- **Refresh Count** - Number of display refreshes

### Viewing in Home Assistant:
1. Go to Settings → Devices & Services → MQTT
2. Look for "TRMNL Weather Station"
3. All sensors will be automatically available

## Display Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Stretford          Sat, 28 Nov 2025  14:30      🔋 85%  📶  │
├─────────────────────────────────────────────────────────────┤
│                                    ┌──────────────────────┐ │
│  12°C                              │  INDOOR              │ │
│  Partly Cloudy                     │  21.5°C    45%       │ │
│  Feels like: 10°C                  └──────────────────────┘ │
│  High: 14°C  Low: 8°C              Sunrise: 07:42          │
│                                    Sunset:  16:12          │
│  Humidity: 78%                                              │
│  Wind: 12 mph SW                                            │
│  Pressure: 1013 mb                                          │
│  UV Index: 2.0                                              │
├─────────────────────────────────────────────────────────────┤
│ Next 12 Hours:                                               │
│ 15:00  16:00  17:00  18:00  19:00  20:00  ...              │
│  12°    11°    10°    9°     8°     7°                     │
├─────────────────────────────────────────────────────────────┤
│ 5-Day Forecast:                                             │
│ Sun    Mon    Tue    Wed    Thu                             │
│ 14/8°  12/6°  13/7°  11/5°  10/4°                          │
└─────────────────────────────────────────────────────────────┘
```

## Sleep Schedule

- **Normal**: Refreshes every 30 minutes
- **Bed Time** (23:00 - 06:00): Sleeps until morning
- **Low Battery**: Refreshes every 60 minutes
- **Critical Battery**: Hibernates until charged

## Troubleshooting

### Device won't connect to WiFi
- Long-press the button (5+ seconds) to reset WiFi credentials
- Reconnect to TRMNL-Weather AP and reconfigure

### Weather data not loading
- Check your OpenWeatherMap API key is valid
- Ensure you have an active internet connection
- Check serial output for error messages

### Indoor data showing N/A
- Verify Home Assistant is accessible at 10.0.1.24
- Check the access token is valid
- Ensure sensor entities exist and are available

### MQTT not working
- Verify MQTT broker is running on Home Assistant
- Check username/password are correct
- Check serial output for MQTT connection errors

## OTA Auto-Update

The device automatically checks for firmware updates from GitHub releases.

### How it works:
- Checks for updates every **12 wake cycles** (~6 hours at 30-minute intervals)
- Downloads firmware from: `https://github.com/Dreadmond/TRMNL-Weather-Display/releases/latest`
- Looks for a `.bin` file in the release assets named `firmware.bin`
- Only updates if a newer version is detected
- Automatically restarts after successful update

### To create a release:
1. Build your firmware: `pio run -t upload` (or just build without uploading)
2. Find the `.bin` file in `.pio/build/trmnl_og/firmware.bin`
3. Create a new GitHub release with version tag (e.g., `v1.0.1`)
4. Upload the `firmware.bin` file as a release asset
5. The device will automatically update on the next check cycle

### Update frequency:
- Default: Every 12 wake cycles (configurable in `src/ota_update.cpp`)
- Only checks if battery is above low battery threshold
- Skips update check if already checked the same version

## Watchdog Timer

A watchdog timer prevents device freezes by automatically sleeping the device after 30 seconds of operation.

### Features:
- **30-second timeout**: Device will force sleep if operation takes longer than 30 seconds
- **Hardware watchdog**: Uses ESP32's task watchdog timer
- **Automatic recovery**: If watchdog triggers, device will reset and continue normal operation
- **Feeds throughout execution**: Watchdog is fed regularly during normal operations

### Configuration:
- Timeout is set in `src/watchdog.cpp` (default: 30 seconds)
- Can be adjusted in `src/main.cpp` where `initWatchdog(30)` is called

## Debug Mode

To enable debug output over serial:

```bash
pio run -e trmnl_og_debug -t upload
```

Then monitor serial output:
```bash
pio device monitor
```

## License

This project combines elements from:
- TRMNL firmware (MIT License)
- LMARZEN's esp32-weather-epd (GPL-3.0)

Please respect the licenses of the original projects.

