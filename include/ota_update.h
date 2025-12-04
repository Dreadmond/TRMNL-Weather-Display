/* OTA Update functionality for TRMNL OG Weather Station
 * Checks GitHub releases for firmware updates
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

// Check for updates and install if available
// Returns true if update was performed (device will restart)
// Returns false if no update available or update failed
bool checkAndPerformOTAUpdate();

// Get current firmware version
String getFirmwareVersion();

#endif // OTA_UPDATE_H

