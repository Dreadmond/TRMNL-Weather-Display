/* Configuration for TRMNL OG Weather Station
 * Based on esp32-weather-epd by Luke Marzen
 * Configured for UK (Stretford) with Home Assistant integration
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <cstdint>
#include <Arduino.h>

// =============================================================================
// E-PAPER PANEL - TRMNL OG uses 7.5" 800x480 Black/White
// =============================================================================
#define DISP_BW_V2

// E-PAPER DRIVER BOARD - TRMNL uses DESPI-C02 compatible driver
#define DRIVER_DESPI_C02

// =============================================================================
// INDOOR SENSOR - We use Home Assistant, not BME sensor
// Define NO_SENSOR to disable BME280/BME680 code paths
// =============================================================================
#define NO_SENSOR
#define USE_HOME_ASSISTANT_INDOOR

// Dummy definition to satisfy config validation (won't actually be used)
#define SENSOR_BME280

// =============================================================================
// LOCALE - UK English
// =============================================================================
#define LOCALE en_GB

// =============================================================================
// UNITS - UK Settings
// =============================================================================
// Temperature: Celsius
#define UNITS_TEMP_CELSIUS

// Wind Speed: Miles per Hour (UK uses mph on roads)
#define UNITS_SPEED_MILESPERHOUR

// Pressure: Hectopascals (millibars)
#define UNITS_PRES_HECTOPASCALS

// Visibility: Kilometers
#define UNITS_DIST_KILOMETERS

// Hourly Precipitation: Probability of Precipitation
#define UNITS_HOURLY_PRECIP_POP

// Daily Precipitation: Millimeters
#define UNITS_DAILY_PRECIP_MILLIMETERS

// =============================================================================
// HTTP SETTINGS
// =============================================================================
#define USE_HTTPS_NO_CERT_VERIF

// =============================================================================
// WIND INDICATOR
// =============================================================================
// Wind direction - use CARDINAL (4 directions) to minimize flash usage
#define WIND_INDICATOR_ARROW
#define WIND_INDICATOR_CPN_CARDINAL
#define WIND_ICONS_CARDINAL

// =============================================================================
// FONTS
// =============================================================================
#define FONT_HEADER "fonts/FreeSans.h"

// =============================================================================
// DISPLAY OPTIONS
// =============================================================================
#define DISPLAY_DAILY_PRECIP 2        // Smart: show only when precipitation forecasted
#define DISPLAY_HOURLY_ICONS 0        // Disabled - icons were floating above graph
#define DISPLAY_ALERTS 1              // Show weather alerts

// =============================================================================
// STATUS BAR
// =============================================================================
#define STATUS_BAR_EXTRAS_BAT_PERCENTAGE 1
#define STATUS_BAR_EXTRAS_BAT_VOLTAGE    0
#define STATUS_BAR_EXTRAS_WIFI_STRENGTH  1
#define STATUS_BAR_EXTRAS_WIFI_RSSI      0

// =============================================================================
// BATTERY
// =============================================================================
#define BATTERY_MONITORING 1

// =============================================================================
// NVS NAMESPACE
// =============================================================================
#define NVS_NAMESPACE "trmnl_weather"

// =============================================================================
// DEBUG
// =============================================================================
#define DEBUG_LEVEL 1

// =============================================================================
// PIN DEFINITIONS - Set in config.cpp for TRMNL OG hardware
// =============================================================================
#define PIN_BUTTON 2  // BOOT button on ESP32-C3
extern const uint8_t PIN_BAT_ADC;
extern const uint8_t PIN_EPD_BUSY;
extern const uint8_t PIN_EPD_CS;
extern const uint8_t PIN_EPD_RST;
extern const uint8_t PIN_EPD_DC;
extern const uint8_t PIN_EPD_SCK;
extern const uint8_t PIN_EPD_MISO;
extern const uint8_t PIN_EPD_MOSI;
extern const uint8_t PIN_EPD_PWR;
extern const uint8_t PIN_BME_SDA;
extern const uint8_t PIN_BME_SCL;
extern const uint8_t PIN_BME_PWR;
extern const uint8_t BME_ADDRESS;

// =============================================================================
// RUNTIME CONFIGURATION - Set in config.cpp
// =============================================================================
extern const char *WIFI_SSID;
extern const char *WIFI_PASSWORD;
extern const unsigned long WIFI_TIMEOUT;
extern const unsigned HTTP_CLIENT_TCP_TIMEOUT;
extern const String OWM_APIKEY;
extern const String OWM_ENDPOINT;
extern const String OWM_ONECALL_VERSION;
extern const String LAT;
extern const String LON;
extern const String CITY_STRING;
extern const char *TIMEZONE;
extern const char *TIME_FORMAT;
extern const char *HOUR_FORMAT;
extern const char *DATE_FORMAT;
extern const char *REFRESH_TIME_FORMAT;
extern const char *NTP_SERVER_1;
extern const char *NTP_SERVER_2;
extern const unsigned long NTP_TIMEOUT;
extern const int SLEEP_DURATION;
extern const int BED_TIME;
extern const int WAKE_TIME;
extern const int HOURLY_GRAPH_MAX;
extern const uint32_t WARN_BATTERY_VOLTAGE;
extern const uint32_t LOW_BATTERY_VOLTAGE;
extern const uint32_t VERY_LOW_BATTERY_VOLTAGE;
extern const uint32_t CRIT_LOW_BATTERY_VOLTAGE;
extern const unsigned long LOW_BATTERY_SLEEP_INTERVAL;
extern const unsigned long VERY_LOW_BATTERY_SLEEP_INTERVAL;
extern const uint32_t MAX_BATTERY_VOLTAGE;
extern const uint32_t MIN_BATTERY_VOLTAGE;

// =============================================================================
// HOME ASSISTANT CONFIGURATION
// =============================================================================
extern const char *HA_HOST;
extern const int HA_PORT;
extern const char *HA_TOKEN;
extern const char *HA_TEMP_ENTITY;
extern const char *HA_HUMIDITY_ENTITY;

// =============================================================================
// MQTT CONFIGURATION
// =============================================================================
extern const char *MQTT_BROKER;
extern const int MQTT_PORT;
extern const char *MQTT_USERNAME;
extern const char *MQTT_PASSWORD;
extern const char *MQTT_CLIENT_ID;

// Nextcloud WebDAV for images
extern const char *NEXTCLOUD_URL;
extern const char *NEXTCLOUD_USER;
extern const char *NEXTCLOUD_PASS;
extern const char *NEXTCLOUD_PHOTO;
extern const char *NEXTCLOUD_CARTOON;

// Display modes
enum DisplayMode {
  MODE_WEATHER = 0,
  MODE_CARTOON = 1,
  MODE_COUNT = 3
};

#endif // __CONFIG_H__
