#pragma once
#include <Arduino.h>
#include <TinyGsmClient.h>

class ConfigManager;

// Manages system time synchronization with NTP (primary) and fallback to mobile network.
// - At initialization: Enable WiFi STA mode, sync from NTP server, disable WiFi
// - Clock increments every second based on millis()
// - Resynchronizes from NTP every 4 hours (or configured interval)
// - Falls back to mobile network time if NTP is unavailable
// - Provides methods to get current time and formatted timestamps
class ClockManager {
public:
    ClockManager(TinyGsm &modem, ConfigManager &configManager);
    ~ClockManager();

    // Initialize clock from NTP (primary), fallback to mobile network time
    // Returns true if successfully synced, false if neither source available
    bool init();

    // Called periodically from loop() to resync with NTP every 4 hours (or configured)
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
    // Synchronize time from NTP via WiFi
    bool syncWithNTP();

    // Synchronize time from mobile network (fallback)
    bool syncWithMobileNetwork();

    // Enable WiFi in STA mode
    bool enableWiFi();

    // Disable WiFi (turn it off)
    void disableWiFi();

    // Wait for WiFi connection
    bool waitForWiFiConnection(unsigned long timeoutMs = 30000);

    // Calculate current time based on initial sync and elapsed milliseconds
    unsigned long calculateCurrentTime();

    TinyGsm    &_modem;
    ConfigManager &_configManager;
    bool       _initialized;
    unsigned long _epochTime;           // Last synced Unix timestamp
    unsigned long _millisecondsAtSync;  // millis() value when we synced
    unsigned long _lastNtpSyncTime;     // millis() of last NTP sync
    unsigned long _ntpResyncIntervalMs; // Resync interval in milliseconds
    int        _tzOffsetSec;            // Timezone offset in seconds (from network)

    // Detect network timezone from modem
    bool detectNetworkTimezone();

    // NTP configuration
    static const char* NTP_SERVER1;
    static const char* NTP_SERVER2;
    static const char* NTP_SERVER3;
};
