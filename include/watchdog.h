/* Watchdog timer functionality for TRMNL OG Weather Station
 * Prevents device freezes by forcing sleep after timeout
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

// Initialize watchdog with timeout in seconds
void initWatchdog(unsigned int timeoutSeconds);

// Feed the watchdog (call this regularly during operation)
void feedWatchdog();

// Check if watchdog triggered (device was reset by watchdog)
bool wasWatchdogReset();

// Force sleep if timeout exceeded (call this before critical operations)
void watchdogCheckAndSleep(unsigned long startTime, unsigned int maxSeconds);

#endif // WATCHDOG_H

