/* TRMNL OG Weather Station
 * Based on esp32-weather-epd by Luke Marzen
 * Modified for TRMNL OG hardware with Home Assistant integration
 *
 * Copyright (C) 2022-2025  Luke Marzen (original)
 * Modified for TRMNL OG by Cursor AI
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "_locale.h"
#include "api_response.h"
#include "client_utils.h"
#include "display_utils.h"
#include "renderer.h"
#include "watchdog.h"
#include "ota_update.h"
#include "icons/icons_196x196.h"
#include FONT_HEADER  // For fonts in image display mode
#include <PNGdec.h>   // For PNG decoding
#include <esp_heap_caps.h>  // For heap_caps_malloc

// No BME sensor - we use Home Assistant for indoor readings

#if defined(USE_HTTPS_WITH_CERT_VERIF) || defined(USE_HTTPS_NO_CERT_VERIF)
  #include <WiFiClientSecure.h>
#endif
#ifdef USE_HTTPS_WITH_CERT_VERIF
  #include "cert.h"
#endif

// API response structures - too large for stack, allocate statically
static owm_resp_onecall_t       owm_onecall;
static owm_resp_air_pollution_t owm_air_pollution;

// NVS preferences
Preferences prefs;

// MQTT client - IMPORTANT: Increase buffer size for HA auto-discovery JSON
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
// Note: setBufferSize(512) called in publishBatteryMQTT

// WiFiManager
WiFiManager wifiManager;

// PIN_BUTTON is now defined in config.h

// RTC memory survives deep sleep - store display mode
RTC_DATA_ATTR DisplayMode currentDisplayMode = MODE_WEATHER;
RTC_DATA_ATTR unsigned long lastButtonPressTime = 0;

// Double-tap detection constants
#define DOUBLE_TAP_WINDOW_MS 800  // Max time between taps for double-tap

/* Fetch indoor temperature from Home Assistant using HTTPClient */
float getHomeAssistantSensorState(const char* entity_id)
{
  HTTPClient http;
  
  String url = String("http://") + HA_HOST + ":" + String(HA_PORT) + "/api/states/" + entity_id;
  
  Serial.print("Fetching HA sensor: ");
  Serial.println(entity_id);
  
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("HA request failed, code: %d\n", httpCode);
    http.end();
    return NAN;
  }
  
  String payload = http.getString();
  http.end();
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error)
  {
    Serial.print("HA JSON parse error: ");
    Serial.println(error.c_str());
    Serial.print("Payload: ");
    Serial.println(payload.substring(0, 100));  // Print first 100 chars for debug
    return NAN;
  }
  
  const char* state = doc["state"];
  if (state && strcmp(state, "unavailable") != 0 && strcmp(state, "unknown") != 0)
  {
    float value = atof(state);
    Serial.printf("HA %s = %.1f\n", entity_id, value);
    return value;
  }
  
  Serial.println("HA sensor unavailable or unknown");
  return NAN;
}

// PNG decoder instance and image buffer (global for callback access)
PNG png;
uint8_t* pngImageBuffer = nullptr;
int pngImageSize = 0;

// PNG draw callback - called for each line of pixels (must return int)
int pngDrawCallback(PNGDRAW *pDraw)
{
  uint16_t *pPixels = (uint16_t *)pDraw->pUser;
  
  // For grayscale/RGB PNG, convert to 1-bit and draw
  // pDraw->y is the current line, pDraw->iWidth is the width
  for (int x = 0; x < pDraw->iWidth; x++)
  {
    uint8_t pixel;
    
    if (pDraw->iBpp == 1) {
      // 1-bit image - get the bit value
      int byteIdx = x / 8;
      int bitIdx = 7 - (x % 8);
      pixel = (pDraw->pPixels[byteIdx] >> bitIdx) & 1;
      // In 1-bit PNG: 0=black, 1=white typically
      if (pixel == 0)
        display.drawPixel(x, pDraw->y, GxEPD_BLACK);
    }
    else if (pDraw->iBpp == 8) {
      // 8-bit grayscale
      pixel = pDraw->pPixels[x];
      // Threshold at 128 for black/white
      if (pixel < 128)
        display.drawPixel(x, pDraw->y, GxEPD_BLACK);
    }
    else if (pDraw->iBpp == 24 || pDraw->iBpp == 32) {
      // RGB or RGBA - convert to grayscale using luminance
      int idx = x * (pDraw->iBpp / 8);
      uint8_t r = pDraw->pPixels[idx];
      uint8_t g = pDraw->pPixels[idx + 1];
      uint8_t b = pDraw->pPixels[idx + 2];
      // Luminance formula
      uint8_t gray = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
      if (gray < 128)
        display.drawPixel(x, pDraw->y, GxEPD_BLACK);
    }
  }
  return 1;  // Return 1 to continue decoding
}

// Simple base64 encode function
String base64Encode(const String &input)
{
  const char* base64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String output = "";
  int i = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];
  int len = input.length();
  const char* bytes = input.c_str();
  
  while (len--) {
    char_array_3[i++] = *(bytes++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;
      for (i = 0; i < 4; i++) output += base64chars[char_array_4[i]];
      i = 0;
    }
  }
  if (i) {
    int j;
    for (j = i; j < 3; j++) char_array_3[j] = '\0';
    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    for (j = 0; j < i + 1; j++) output += base64chars[char_array_4[j]];
    while (i++ < 3) output += '=';
  }
  return output;
}

/* Fetch PNG from Nextcloud WebDAV and display it */
bool fetchAndDisplayNextcloudImage(const char* filename)
{
  WiFiClientSecure client;
  client.setInsecure();  // Skip cert verification for Nextcloud
  client.setTimeout(30000);
  
  HTTPClient http;
  String url = String(NEXTCLOUD_URL) + filename;
  
  Serial.print("Fetching image: ");
  Serial.println(url);
  
  http.begin(client, url);
  
  // Basic auth for WebDAV
  String auth = String(NEXTCLOUD_USER) + ":" + String(NEXTCLOUD_PASS);
  String authEncoded = base64Encode(auth);
  
  Serial.print("Auth header: Basic ");
  Serial.println(authEncoded);
  
  http.addHeader("Authorization", "Basic " + authEncoded);
  http.addHeader("User-Agent", "TRMNL-Weather/1.0");  // Some servers require User-Agent
  http.setTimeout(30000);  // 30 second timeout for image download
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Follow redirects
  
  Serial.println("Sending HTTP GET...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("Nextcloud request failed, code: %d\n", httpCode);
    if (httpCode > 0) {
      Serial.print("Response: ");
      Serial.println(http.getString().substring(0, 200));  // Print first 200 chars of response
    }
    http.end();
    return false;
  }
  
  int contentLength = http.getSize();
  Serial.printf("Image size: %d bytes\n", contentLength);
  
  if (contentLength <= 0 || contentLength > 500000)
  {
    Serial.println("Invalid content length");
    http.end();
    return false;
  }
  
  // Try to allocate buffer - use smaller chunks if needed due to heap fragmentation
  // First, try to free up memory by running garbage collection
  Serial.printf("Free heap: %d, Max block: %d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  
  pngImageBuffer = (uint8_t*)malloc(contentLength);
  if (!pngImageBuffer)
  {
    // Try with ps_malloc if available, or heap_caps_malloc
    pngImageBuffer = (uint8_t*)heap_caps_malloc(contentLength, MALLOC_CAP_8BIT);
  }
  if (!pngImageBuffer)
  {
    Serial.println("Failed to allocate image buffer - trying smaller buffer");
    // If still failing, the image is too large for available memory
    // Show error and return
    http.end();
    
    initDisplay();
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FONT_14pt8b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(100, 200);
    display.print("Image too large for memory");
    display.setCursor(100, 240);
    display.setFont(&FONT_10pt8b);
    display.printf("Need: %dKB, Have: %dKB", contentLength/1024, ESP.getMaxAllocHeap()/1024);
    display.setCursor(100, 280);
    display.print("Press button to change mode");
    while (display.nextPage());
    powerOffDisplay();
    return true;  // Return true so we don't show another error
  }
  
  // Read image data
  WiFiClient* stream = http.getStreamPtr();
  int bytesRead = 0;
  int lastPercent = 0;
  while (http.connected() && bytesRead < contentLength)
  {
    size_t available = stream->available();
    if (available)
    {
      int toRead = min(available, (size_t)(contentLength - bytesRead));
      int read = stream->readBytes(pngImageBuffer + bytesRead, toRead);
      bytesRead += read;
      
      // Progress indication
      int percent = (bytesRead * 100) / contentLength;
      if (percent != lastPercent && percent % 10 == 0) {
        Serial.printf("Download: %d%%\n", percent);
        lastPercent = percent;
      }
    }
    delay(1);
  }
  http.end();
  
  Serial.printf("Downloaded %d bytes\n", bytesRead);
  pngImageSize = bytesRead;
  
  if (bytesRead != contentLength)
  {
    Serial.println("Incomplete download");
    free(pngImageBuffer);
    pngImageBuffer = nullptr;
    return false;
  }
  
  // Verify PNG signature
  if (bytesRead < 8 || pngImageBuffer[0] != 0x89 || pngImageBuffer[1] != 'P' || 
      pngImageBuffer[2] != 'N' || pngImageBuffer[3] != 'G')
  {
    Serial.println("Invalid PNG file");
    free(pngImageBuffer);
    pngImageBuffer = nullptr;
    return false;
  }
  
  Serial.println("Valid PNG detected, decoding...");
  
  // Initialize display first (before decoding)
  initDisplay();
  display.fillScreen(GxEPD_WHITE);
  
  // Open PNG from memory
  int rc = png.openRAM(pngImageBuffer, pngImageSize, pngDrawCallback);
  if (rc == PNG_SUCCESS)
  {
    Serial.printf("PNG: %d x %d, %d bpp\n", png.getWidth(), png.getHeight(), png.getBpp());
    
    // Decode and draw - the callback will draw each line
    rc = png.decode(nullptr, 0);
    if (rc != PNG_SUCCESS)
    {
      Serial.printf("PNG decode failed: %d\n", rc);
    }
    else
    {
      Serial.println("PNG decoded successfully");
    }
    png.close();
  }
  else
  {
    Serial.printf("PNG open failed: %d\n", rc);
    // Show error on display
    display.setFont(&FONT_14pt8b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(200, 240);
    display.print("PNG decode error");
  }
  
  // Finish display update
  while (display.nextPage());
  powerOffDisplay();
  
  free(pngImageBuffer);
  pngImageBuffer = nullptr;
  return true;
}

/* Check for double-tap and cycle display mode */
bool checkDoubleTap()
{
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO)
  {
    unsigned long currentTime = millis();
    // Check if this is second tap within window
    // Note: lastButtonPressTime is preserved in RTC memory, but millis() resets
    // We need to use a different approach - store timestamp in NVS or use RTC
    
    // Simple approach: always cycle mode on button press
    // (true double-tap detection would require staying awake briefly)
    
    // For now: single press cycles through modes
    currentDisplayMode = (DisplayMode)((currentDisplayMode + 1) % MODE_COUNT);
    Serial.printf("Button press - switching to mode: %d\n", currentDisplayMode);
    return true;
  }
  
  return false;
}

/* Publish battery telemetry to Home Assistant via MQTT */
void publishBatteryMQTT(uint32_t batteryVoltage)
{
  if (!mqttClient.connected())
  {
    Serial.print("Connecting to MQTT...");
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    // CRITICAL: Increase buffer size for HA auto-discovery payloads (default 256 is too small!)
    mqttClient.setBufferSize(512);
    
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD))
    {
      Serial.println("connected");
      
      // Publish auto-discovery config for Home Assistant
      JsonDocument configDoc;
      configDoc["name"] = "TRMNL Battery";
      configDoc["state_topic"] = "trmnl_weather/state";
      configDoc["unit_of_measurement"] = "%";
      configDoc["device_class"] = "battery";
      configDoc["state_class"] = "measurement";
      configDoc["value_template"] = "{{ value_json.battery }}";
      configDoc["unique_id"] = "trmnl_weather_battery";
      
      JsonObject device = configDoc["device"].to<JsonObject>();
      JsonArray identifiers = device["identifiers"].to<JsonArray>();
      identifiers.add("trmnl_weather_station");
      device["name"] = "TRMNL Weather Station";
      device["model"] = "TRMNL OG";
      device["manufacturer"] = "TRMNL";
      device["sw_version"] = getFirmwareVersion();
      
      String configPayload;
      serializeJson(configDoc, configPayload);
      Serial.print("MQTT battery config: ");
      Serial.println(configPayload);
      
      bool ok1 = mqttClient.publish("homeassistant/sensor/trmnl_weather_battery/config", 
                                    configPayload.c_str(), true);
      Serial.print("Battery config publish: ");
      Serial.println(ok1 ? "OK" : "FAILED");
      mqttClient.loop();
      
      // Also publish voltage sensor
      JsonDocument voltDoc;
      voltDoc["name"] = "TRMNL Battery Voltage";
      voltDoc["state_topic"] = "trmnl_weather/state";
      voltDoc["unit_of_measurement"] = "mV";
      voltDoc["device_class"] = "voltage";
      voltDoc["state_class"] = "measurement";
      voltDoc["value_template"] = "{{ value_json.voltage }}";
      voltDoc["unique_id"] = "trmnl_weather_voltage";
      voltDoc["device"] = device;
      
      String voltPayload;
      serializeJson(voltDoc, voltPayload);
      Serial.print("MQTT voltage config: ");
      Serial.println(voltPayload);
      
      bool ok2 = mqttClient.publish("homeassistant/sensor/trmnl_weather_voltage/config",
                                    voltPayload.c_str(), true);
      Serial.print("Voltage config publish: ");
      Serial.println(ok2 ? "OK" : "FAILED");
      mqttClient.loop();
    }
    else
    {
      Serial.print("MQTT failed, rc=");
      Serial.println(mqttClient.state());
      return;
    }
  }
  
  // Calculate battery percentage
  int batteryPercent = 0;
  if (batteryVoltage >= MAX_BATTERY_VOLTAGE)
  {
    batteryPercent = 100;
  }
  else if (batteryVoltage <= MIN_BATTERY_VOLTAGE)
  {
    batteryPercent = 0;
  }
  else
  {
    batteryPercent = map(batteryVoltage, MIN_BATTERY_VOLTAGE, MAX_BATTERY_VOLTAGE, 0, 100);
  }
  
  // Publish state
  JsonDocument stateDoc;
  stateDoc["battery"] = batteryPercent;
  stateDoc["voltage"] = batteryVoltage;
  
  String statePayload;
  serializeJson(stateDoc, statePayload);
  Serial.print("MQTT state: ");
  Serial.println(statePayload);
  
  if (mqttClient.publish("trmnl_weather/state", statePayload.c_str(), true))
  {
    Serial.println("Battery telemetry published");
  }
  else
  {
    Serial.println("MQTT publish failed");
  }
  
  // Process MQTT packets and give time for messages to be sent
  for (int i = 0; i < 10; i++) {
    mqttClient.loop();
    delay(50);
  }
  
  mqttClient.disconnect();
  Serial.println("MQTT disconnected");
}

/* Start WiFi with WiFiManager for captive portal support */
wl_status_t startWiFiManager(int &wifiRSSI)
{
  Serial.println("Starting WiFiManager...");
  
  // Check if button is held for WiFi reset
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  if (digitalRead(PIN_BUTTON) == LOW)
  {
    unsigned long pressStart = millis();
    while (digitalRead(PIN_BUTTON) == LOW && millis() - pressStart < 5000)
    {
      delay(100);
    }
    if (millis() - pressStart >= 5000)
    {
      Serial.println("Button held - resetting WiFi credentials");
      wifiManager.resetSettings();
    }
  }
  
  // Configure WiFiManager
  wifiManager.setConfigPortalTimeout(180);  // 3 minutes
  wifiManager.setConnectTimeout(30);         // 30 seconds
  
  // Try to connect, start portal if fails
  bool connected = wifiManager.autoConnect("TRMNL-Weather-Setup");
  
  if (connected)
  {
    wifiRSSI = WiFi.RSSI();
    Serial.print("WiFi connected, RSSI: ");
    Serial.println(wifiRSSI);
    return WL_CONNECTED;
  }
  else
  {
    Serial.println("WiFi connection failed");
    return WL_CONNECT_FAILED;
  }
}

/* Put esp32 into ultra low-power deep sleep */
void beginDeepSleep(unsigned long startTime, tm *timeInfo)
{
  if (!getLocalTime(timeInfo))
  {
    Serial.println(TXT_REFERENCING_OLDER_TIME_NOTICE);
  }

  int bedtimeHour = INT_MAX;
  if (BED_TIME != WAKE_TIME)
  {
    bedtimeHour = (BED_TIME - WAKE_TIME + 24) % 24;
  }

  int curHour = (timeInfo->tm_hour - WAKE_TIME + 24) % 24;
  const int curMinute = curHour * 60 + timeInfo->tm_min;
  const int curSecond = curHour * 3600
                      + timeInfo->tm_min * 60
                      + timeInfo->tm_sec;
  const int desiredSleepSeconds = SLEEP_DURATION * 60;
  const int offsetMinutes = curMinute % SLEEP_DURATION;
  const int offsetSeconds = curSecond % desiredSleepSeconds;

  int sleepMinutes = SLEEP_DURATION - offsetMinutes;
  if (desiredSleepSeconds - offsetSeconds < 120
   || offsetSeconds / (float)desiredSleepSeconds > 0.95f)
  {
    sleepMinutes += SLEEP_DURATION;
  }

  const int predictedWakeHour = ((curMinute + sleepMinutes) / 60) % 24;

  uint64_t sleepDuration;
  if (predictedWakeHour < bedtimeHour)
  {
    sleepDuration = sleepMinutes * 60 - timeInfo->tm_sec;
  }
  else
  {
    const int hoursUntilWake = 24 - curHour;
    sleepDuration = hoursUntilWake * 3600ULL
                    - (timeInfo->tm_min * 60ULL + timeInfo->tm_sec);
  }

  sleepDuration += 3ULL;
  sleepDuration *= 1.0015f;

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  // Enable timer wake
  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  
  // Enable GPIO wake for button press (ESP32-C3)
  // Configure button pin for wake
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
  
  Serial.print(TXT_AWAKE_FOR);
  Serial.println(" "  + String((millis() - startTime) / 1000.0, 3) + "s");
  Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
  Serial.println(" " + String(sleepDuration) + "s");
  Serial.println("Press button to wake early");
  Serial.flush();
  esp_deep_sleep_start();
}

/* Program entry point */
void setup()
{
  unsigned long startTime = millis();
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n========================================");
  Serial.println("   TRMNL OG Weather Station");
  Serial.println("   Based on esp32-weather-epd");
  Serial.print("   Firmware Version: ");
  Serial.println(getFirmwareVersion());
  Serial.println("========================================\n");
  
  // Initialize watchdog timer (30 second timeout)
  initWatchdog(30);
  feedWatchdog();
  
  // Check if we were reset by watchdog
  if (wasWatchdogReset()) {
    Serial.println("WARNING: Device was reset by watchdog - possible freeze detected!");
  }

  // Check wake reason and handle button press
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool buttonPressed = false;
  bool doubleTap = false;
  
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wake: Timer");
      break;
    case ESP_SLEEP_WAKEUP_GPIO: {
      Serial.println("Wake: Button press - checking for double tap...");
      buttonPressed = true;
      
      // Wait for button release first
      pinMode(PIN_BUTTON, INPUT_PULLUP);
      while (digitalRead(PIN_BUTTON) == LOW) {
        delay(10);
      }
      
      // Now wait up to 500ms for a second press (double-tap detection)
      unsigned long startWait = millis();
      while (millis() - startWait < 500) {
        if (digitalRead(PIN_BUTTON) == LOW) {
          // Second press detected - double tap!
          doubleTap = true;
          Serial.println("Double tap detected!");
          // Wait for release
          while (digitalRead(PIN_BUTTON) == LOW) {
            delay(10);
          }
          break;
        }
        delay(10);
      }
      
      if (doubleTap) {
        // Double tap - change mode
        currentDisplayMode = (DisplayMode)((currentDisplayMode + 1) % MODE_COUNT);
        Serial.printf("Mode changed to: %d (%s)\n", currentDisplayMode,
          currentDisplayMode == MODE_WEATHER ? "Weather" : "Cartoon");
      } else {
        // Single tap - just refresh current mode
        Serial.printf("Single tap - refreshing mode: %d (%s)\n", currentDisplayMode,
          currentDisplayMode == MODE_WEATHER ? "Weather" : "Cartoon");
      }
      break;
    }
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wake: External");
      break;
    default:
      Serial.println("Wake: Power on / Reset");
      currentDisplayMode = MODE_WEATHER;  // Reset to weather on power-on
      break;
  }

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  disableBuiltinLED();

  // Open namespace for read/write to non-volatile storage
  prefs.begin(NVS_NAMESPACE, false);

#if BATTERY_MONITORING
  uint32_t batteryVoltage = readBatteryVoltage();
  Serial.print(TXT_BATTERY_VOLTAGE);
  Serial.println(": " + String(batteryVoltage) + "mv");

  bool lowBat = prefs.getBool("lowBat", false);

  if (batteryVoltage <= LOW_BATTERY_VOLTAGE)
  {
    if (lowBat == false)
    {
      prefs.putBool("lowBat", true);
      prefs.end();
      initDisplay();
      do
      {
        drawError(battery_alert_0deg_196x196, TXT_LOW_BATTERY);
      } while (display.nextPage());
      powerOffDisplay();
    }

    // Enable button wake for all low battery cases
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    
    if (batteryVoltage <= CRIT_LOW_BATTERY_VOLTAGE)
    {
      Serial.println(TXT_CRIT_LOW_BATTERY_VOLTAGE);
      Serial.println(TXT_HIBERNATING_INDEFINITELY_NOTICE);
      Serial.println("Press button to wake");
    }
    else if (batteryVoltage <= VERY_LOW_BATTERY_VOLTAGE)
    {
      esp_sleep_enable_timer_wakeup(VERY_LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
      Serial.println(TXT_VERY_LOW_BATTERY_VOLTAGE);
      Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
      Serial.println(" " + String(VERY_LOW_BATTERY_SLEEP_INTERVAL) + "min");
    }
    else
    {
      esp_sleep_enable_timer_wakeup(LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
      Serial.println(TXT_LOW_BATTERY_VOLTAGE);
      Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
      Serial.println(" " + String(LOW_BATTERY_SLEEP_INTERVAL) + "min");
    }
    Serial.flush();
    esp_deep_sleep_start();
  }

  if (lowBat == true)
  {
    prefs.putBool("lowBat", false);
  }
#else
  uint32_t batteryVoltage = UINT32_MAX;
#endif

  prefs.end();

  String statusStr = {};
  String tmpStr = {};
  tm timeInfo = {};

  // START WIFI using WiFiManager
  int wifiRSSI = 0;
  watchdogCheckAndSleep(startTime, 30);
  wl_status_t wifiStatus = startWiFiManager(wifiRSSI);
  if (wifiStatus != WL_CONNECTED)
  {
    killWiFi();
    initDisplay();
    Serial.println(TXT_WIFI_CONNECTION_FAILED);
    do
    {
      drawError(wifi_x_196x196, TXT_WIFI_CONNECTION_FAILED);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }

  feedWatchdog();
  
  // Check for OTA updates (only if WiFi connected and enough battery)
#if BATTERY_MONITORING
  if (batteryVoltage > LOW_BATTERY_VOLTAGE)
#endif
  {
    Serial.println("Checking for firmware updates...");
    watchdogCheckAndSleep(startTime, 30);
    if (checkAndPerformOTAUpdate()) {
      // Update was performed, device will restart
      return;
    }
    feedWatchdog();
  }

  // TIME SYNCHRONIZATION
  watchdogCheckAndSleep(startTime, 30);
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
  bool timeConfigured = waitForSNTPSync(&timeInfo);
  feedWatchdog();
  if (!timeConfigured)
  {
    Serial.println(TXT_TIME_SYNCHRONIZATION_FAILED);
    killWiFi();
    initDisplay();
    do
    {
      drawError(wi_time_4_196x196, TXT_TIME_SYNCHRONIZATION_FAILED);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }

  // =========================================================================
  // PHOTO/CARTOON MODE - Skip weather APIs to preserve memory for images
  // =========================================================================
  if (currentDisplayMode == MODE_CARTOON)
  {
    const char* imageFile = NEXTCLOUD_CARTOON;
    
    Serial.printf("Display mode: %s - fetching %s\n", 
                  "Cartoon",
                  imageFile);
    Serial.printf("Free heap BEFORE image: %d, Max block: %d\n", 
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    
    if (!fetchAndDisplayNextcloudImage(imageFile))
    {
      killWiFi();
      initDisplay();
      do
      {
        drawError(wi_cloud_down_196x196, "Image fetch failed", imageFile);
      } while (display.nextPage());
      powerOffDisplay();
    }
    else
    {
      killWiFi();
    }
    beginDeepSleep(startTime, &timeInfo);
    return;  // Don't continue to weather code
  }

  // =========================================================================
  // WEATHER MODE - Fetch weather data
  // =========================================================================
  // MAKE API REQUESTS
#ifdef USE_HTTP
  WiFiClient client;
#elif defined(USE_HTTPS_NO_CERT_VERIF)
  WiFiClientSecure client;
  client.setInsecure();
#elif defined(USE_HTTPS_WITH_CERT_VERIF)
  WiFiClientSecure client;
  client.setCACert(cert_Sectigo_RSA_Organization_Validation_Secure_Server_CA);
#endif

  feedWatchdog();
  watchdogCheckAndSleep(startTime, 30);
  int rxStatus = getOWMonecall(client, owm_onecall);
  feedWatchdog();
  if (rxStatus != HTTP_CODE_OK)
  {
    killWiFi();
    statusStr = "One Call " + OWM_ONECALL_VERSION + " API";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
    initDisplay();
    do
    {
      drawError(wi_cloud_down_196x196, statusStr, tmpStr);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }

  watchdogCheckAndSleep(startTime, 30);
  rxStatus = getOWMairpollution(client, owm_air_pollution);
  feedWatchdog();
  if (rxStatus != HTTP_CODE_OK)
  {
    killWiFi();
    statusStr = "Air Pollution API";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
    initDisplay();
    do
    {
      drawError(wi_cloud_down_196x196, statusStr, tmpStr);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }

  // GET INDOOR TEMPERATURE AND HUMIDITY from Home Assistant
  float inTemp = NAN;
  float inHumidity = NAN;

#ifdef USE_HOME_ASSISTANT_INDOOR
  watchdogCheckAndSleep(startTime, 30);
  Serial.println("Reading indoor sensors from Home Assistant...");
  inTemp = getHomeAssistantSensorState(HA_TEMP_ENTITY);
  inHumidity = getHomeAssistantSensorState(HA_HUMIDITY_ENTITY);
  feedWatchdog();
  
  if (!std::isnan(inTemp) && !std::isnan(inHumidity))
  {
    Serial.println("Home Assistant indoor readings OK");
  }
  else
  {
    statusStr = "HA sensors unavailable";
    Serial.println(statusStr);
  }
#endif

  // PUBLISH BATTERY TELEMETRY TO HOME ASSISTANT
#if BATTERY_MONITORING
  watchdogCheckAndSleep(startTime, 30);
  publishBatteryMQTT(batteryVoltage);
  feedWatchdog();
#endif

  killWiFi(); // WiFi no longer needed

  String refreshTimeStr;
  getRefreshTimeStr(refreshTimeStr, timeConfigured, &timeInfo);
  String dateStr;
  getDateStr(dateStr, &timeInfo);

  // RENDER WEATHER DISPLAY
  watchdogCheckAndSleep(startTime, 30);
  initDisplay();
  feedWatchdog();
  do
  {
    watchdogCheckAndSleep(startTime, 30);
    drawCurrentConditions(owm_onecall.current, owm_onecall.daily[0],
                          owm_air_pollution, inTemp, inHumidity);
    feedWatchdog();
    drawOutlookGraph(owm_onecall.hourly, owm_onecall.daily, timeInfo);
    feedWatchdog();
    drawForecast(owm_onecall.daily, timeInfo);
    feedWatchdog();
    drawLocationDate(CITY_STRING, dateStr);
#if DISPLAY_ALERTS
    drawAlerts(owm_onecall.alerts, CITY_STRING, dateStr);
    feedWatchdog();
#endif
    drawStatusBar(statusStr, refreshTimeStr, wifiRSSI, batteryVoltage);
    feedWatchdog();
  } while (display.nextPage());
  powerOffDisplay();

  // DEEP SLEEP
  watchdogCheckAndSleep(startTime, 30);
  beginDeepSleep(startTime, &timeInfo);
}

/* This will never run */
void loop()
{
}
