#pragma once
#include <Arduino.h>
#include <TinyGsmClient.h>

// Manages system time synchronization with the network and local time tracking.
// - Synchronizes time from the mobile network on connection
// - Resynchronizes every 5 minutes
// - Increments local time each second based on millis()
// - Provides methods to get current time and formatted timestamps
class ClockManager {
public:
    ClockManager(TinyGsm &modem);
    ~ClockManager();

    // Initialize clock from network time (call after modem connects)
    // Returns true if successfully synced, false if network time unavailable
    bool init();

    // Called periodically from loop() to resync with network every 5 minutes
    void check();

    // Get current Unix timestamp (seconds since epoch)
    unsigned long getCurrentTime();

    // Get formatted date/time string (e.g., "2024-07-17 14:30:45")
    String getFormattedDateTime();

    // Get formatted time only (e.g., "14:30:45")
    String getFormattedTime();

    // Get formatted date only (e.g., "2024-07-17")
    String getFormattedDate();

    // Check if clock has been initialized with network time
    bool isInitialized() const { return _initialized; }

private:
    // Synchronize time from network
    bool syncWithNetwork();

    // Calculate current time based on initial sync and elapsed milliseconds
    unsigned long calculateCurrentTime();

    TinyGsm    &_modem;
    bool       _initialized;
    unsigned long _epochTime;         // Last synced Unix timestamp
    unsigned long _millisecondsAtSync; // millis() value when we synced
    unsigned long _lastSyncTime;      // millis() of last sync check

    static const unsigned long RESYNC_INTERVAL_MS = 5 * 60 * 1000;       // 5 minutes
    static const unsigned long SYNC_CHECK_INTERVAL_MS = 60000;           // Check every 60 seconds
};
