/* Watchdog timer functionality for TRMNL OG Weather Station
 * Prevents device freezes by forcing sleep after timeout
 */

#include "watchdog.h"
#include "config.h"
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include <time.h>
#include <Preferences.h>
#include <Arduino.h>

// Watchdog timeout in seconds (default 30s)
#define WATCHDOG_TIMEOUT_SECONDS 30

static bool watchdogInitialized = false;
static unsigned long watchdogStartTime = 0;
static unsigned int watchdogTimeoutSeconds = WATCHDOG_TIMEOUT_SECONDS;

void initWatchdog(unsigned int timeoutSeconds) {
  if (watchdogInitialized) {
    return; // Already initialized
  }
  
  watchdogTimeoutSeconds = timeoutSeconds;
  watchdogStartTime = millis();
  
  // Initialize Task Watchdog Timer
  // The ESP32 has a hardware watchdog that can reset the chip
  esp_task_wdt_init(timeoutSeconds, true); // true = panic on timeout (restart)
  esp_task_wdt_add(NULL); // Add current task to watchdog
  
  watchdogInitialized = true;
  
  Serial.printf("Watchdog initialized: %d seconds\n", timeoutSeconds);
}

void feedWatchdog() {
  if (!watchdogInitialized) {
    return;
  }
  
  // Feed the watchdog to prevent reset
  esp_task_wdt_reset();
}

bool wasWatchdogReset() {
  // Check if reset was caused by watchdog
  esp_reset_reason_t reason = esp_reset_reason();
  return (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_PANIC);
}

void watchdogCheckAndSleep(unsigned long startTime, unsigned int maxSeconds) {
  unsigned long elapsedSeconds = (millis() - startTime) / 1000;
  
  if (elapsedSeconds >= maxSeconds) {
    Serial.println("WATCHDOG: Maximum runtime exceeded, forcing sleep!");
    Serial.flush();
    
    // Prepare for sleep
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();
    
    tm timeInfo = {};
    // Use current time or default
    time_t now;
    if (time(&now) != -1) {
      localtime_r(&now, &timeInfo);
    }
    
    // Force minimal sleep (1 minute)
    esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    
    Serial.println("Entering emergency sleep mode");
    Serial.flush();
    delay(500);
    esp_deep_sleep_start();
  }
  
  // Feed watchdog
  feedWatchdog();
}

