/* OTA Update functionality for TRMNL OG Weather Station
 * Checks GitHub releases for firmware updates
 */

#include "config.h"
#include "ota_update.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include "client_utils.h"

// Current firmware version - update this when releasing
// This must match the version in your GitHub releases
#define FIRMWARE_VERSION "1.0.1"

// GitHub repository for updates
// Format: owner/repo-name (e.g., "Dreadmond/TRMNL-Weather-Display")
#define GITHUB_REPO_OWNER "Dreadmond"
#define GITHUB_REPO_NAME "TRMNL-Weather-Display"

// Check for updates every N wake cycles (default: every 12 cycles = ~6 hours)
#define OTA_CHECK_INTERVAL 12

// NVS keys
#define NVS_KEY_OTA_CHECK_COUNTER "ota_check_cnt"
#define NVS_KEY_LAST_CHECKED_VERSION "ota_last_v"

String getFirmwareVersion() {
  return String(FIRMWARE_VERSION);
}

/* Compare two version strings (e.g., "1.2.3" vs "1.2.4")
 * Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
 */
int compareVersions(const String& v1, const String& v2) {
  // Remove 'v' prefix if present
  String v1_clean = v1;
  String v2_clean = v2;
  if (v1_clean.startsWith("v") || v1_clean.startsWith("V")) {
    v1_clean = v1_clean.substring(1);
  }
  if (v2_clean.startsWith("v") || v2_clean.startsWith("V")) {
    v2_clean = v2_clean.substring(1);
  }
  
  // Split into parts
  int parts1[3] = {0, 0, 0};
  int parts2[3] = {0, 0, 0};
  
  int idx1 = 0, idx2 = 0;
  String token;
  
  // Parse v1
  for (int i = 0; i <= v1_clean.length(); i++) {
    if (i == v1_clean.length() || v1_clean.charAt(i) == '.') {
      if (idx1 < 3) {
        parts1[idx1++] = token.toInt();
        token = "";
      }
    } else {
      token += v1_clean.charAt(i);
    }
  }
  
  // Parse v2
  token = "";
  for (int i = 0; i <= v2_clean.length(); i++) {
    if (i == v2_clean.length() || v2_clean.charAt(i) == '.') {
      if (idx2 < 3) {
        parts2[idx2++] = token.toInt();
        token = "";
      }
    } else {
      token += v2_clean.charAt(i);
    }
  }
  
  // Compare parts
  for (int i = 0; i < 3; i++) {
    if (parts1[i] < parts2[i]) return -1;
    if (parts1[i] > parts2[i]) return 1;
  }
  
  return 0;
}

/* Check GitHub releases API for latest version */
String getLatestVersionFromGitHub() {
  WiFiClientSecure client;
  client.setInsecure(); // Skip cert verification for GitHub API
  client.setTimeout(10000);
  
  HTTPClient http;
  String url = "https://api.github.com/repos/" + String(GITHUB_REPO_OWNER) + "/" + String(GITHUB_REPO_NAME) + "/releases/latest";
  
  Serial.print("Checking GitHub for updates: ");
  Serial.println(url);
  
  http.begin(client, url);
  http.addHeader("User-Agent", "TRMNL-Weather/1.0");
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("GitHub API request failed, code: %d\n", httpCode);
    http.end();
    return "";
  }
  
  String payload = http.getString();
  http.end();
  
  // Parse JSON response
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return "";
  }
  
  const char* tagName = doc["tag_name"];
  if (tagName) {
    Serial.print("Latest release: ");
    Serial.println(tagName);
    return String(tagName);
  }
  
  return "";
}

/* Get download URL for firmware.bin from latest release */
String getFirmwareDownloadURL(const String& version) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  HTTPClient http;
  String url = "https://api.github.com/repos/" + String(GITHUB_REPO_OWNER) + "/" + String(GITHUB_REPO_NAME) + "/releases/latest";
  
  http.begin(client, url);
  http.addHeader("User-Agent", "TRMNL-Weather/1.0");
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("GitHub API request failed, code: %d\n", httpCode);
    http.end();
    return "";
  }
  
  String payload = http.getString();
  http.end();
  
  // Parse JSON response
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return "";
  }
  
  // Look for firmware.bin in assets
  JsonArray assets = doc["assets"];
  for (JsonObject asset : assets) {
    const char* downloadUrl = asset["browser_download_url"];
    const char* name = asset["name"];
    if (downloadUrl && name && String(name).endsWith(".bin")) {
      Serial.print("Found firmware URL: ");
      Serial.println(downloadUrl);
      return String(downloadUrl);
    }
  }
  
  // Fallback: construct URL directly (GitHub releases pattern)
  String downloadUrl = "https://github.com/" + String(GITHUB_REPO_OWNER) + "/" + String(GITHUB_REPO_NAME) + "/releases/download/" + version + "/firmware.bin";
  Serial.print("Using constructed URL: ");
  Serial.println(downloadUrl);
  return downloadUrl;
}

/* Perform OTA update from GitHub release */
bool performOTAUpdate(const String& firmwareUrl) {
  Serial.println("Starting OTA update...");
  
  WiFiClientSecure client;
  client.setInsecure(); // Skip cert verification
  client.setTimeout(30000);
  
  // Use HTTPUpdate for ESP32
  httpUpdate.setLedPin(-1); // Disable LED (we don't have one)
  
  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl, FIRMWARE_VERSION);
  
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA update failed: %s\n", httpUpdate.getLastErrorString().c_str());
      return false;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No updates available");
      return false;
      
    case HTTP_UPDATE_OK:
      Serial.println("OTA update successful! Restarting...");
      Serial.flush();
      delay(1000);
      ESP.restart();
      return true; // This won't be reached, but indicates success
      
    default:
      Serial.println("Unknown OTA update result");
      return false;
  }
}

/* Main OTA check and update function */
bool checkAndPerformOTAUpdate() {
  // Only check periodically to avoid excessive API calls
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  
  int checkCounter = prefs.getInt(NVS_KEY_OTA_CHECK_COUNTER, 0);
  String lastCheckedVersion = prefs.getString(NVS_KEY_LAST_CHECKED_VERSION, "");
  
  // Increment counter
  checkCounter++;
  prefs.putInt(NVS_KEY_OTA_CHECK_COUNTER, checkCounter);
  
  // Only check if interval has passed
  if (checkCounter < OTA_CHECK_INTERVAL) {
    prefs.end();
    Serial.printf("Skipping OTA check (counter: %d/%d)\n", checkCounter, OTA_CHECK_INTERVAL);
    return false;
  }
  
  // Reset counter
  prefs.putInt(NVS_KEY_OTA_CHECK_COUNTER, 0);
  prefs.end();
  
  Serial.println("Checking for firmware updates...");
  Serial.print("Current version: ");
  Serial.println(FIRMWARE_VERSION);
  
  // Get latest version from GitHub
  String latestVersion = getLatestVersionFromGitHub();
  if (latestVersion.isEmpty()) {
    Serial.println("Failed to get latest version from GitHub");
    return false;
  }
  
  // Check if we already checked this version
  if (lastCheckedVersion == latestVersion) {
    Serial.println("Already checked this version, skipping");
    return false;
  }
  
  // Store checked version
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_KEY_LAST_CHECKED_VERSION, latestVersion);
  prefs.end();
  
  // Compare versions
  int comparison = compareVersions(FIRMWARE_VERSION, latestVersion);
  if (comparison >= 0) {
    Serial.println("Already running latest version");
    return false;
  }
  
  Serial.println("New version available! Downloading...");
  
  // Get download URL
  String firmwareUrl = getFirmwareDownloadURL(latestVersion);
  if (firmwareUrl.isEmpty()) {
    Serial.println("Failed to get firmware download URL");
    return false;
  }
  
  // Perform update (will restart if successful)
  return performOTAUpdate(firmwareUrl);
}

