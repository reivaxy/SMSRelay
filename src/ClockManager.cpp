#include "utilities.h"
#include "secret.h"
#include "ConfigManager.h"
#include <TinyGsmClient.h>
#include <WiFi.h>
#include "ClockManager.h"

// NTP Server addresses
const char* ClockManager::NTP_SERVER1 = "pool.ntp.org";
const char* ClockManager::NTP_SERVER2 = "time.nist.gov";
const char* ClockManager::NTP_SERVER3 = "time.google.com";

ClockManager::ClockManager(TinyGsm &modem, ConfigManager &configManager)
    : _modem(modem), _configManager(configManager), _initialized(false), 
      _syncSource(SYNC_NONE), _epochTime(0), _millisecondsAtSync(0), _lastNtpSyncTime(0), 
      _ntpResyncIntervalMs(4 * 60 * 60 * 1000), _tzOffsetSec(0) {
    log_i("[CLOCK] ClockManager initialized");
}

ClockManager::~ClockManager() {
}

bool ClockManager::init() {
    log_i("[CLOCK] Initializing clock...");
      
    // Try mobile network time first (fastest, already connected)
    log_i("[CLOCK] Attempting to sync time from mobile network...");
    if (syncWithMobileNetwork()) {
        _syncSource = SYNC_MOBILE;
        _initialized = true;
        log_i("[CLOCK] Clock initialized from mobile network: %s", 
              getFormattedDateTime().c_str());
        return true;
    }
    
    // Fall back to NTP (requires WiFi but more reliable)
    log_w("[CLOCK] Mobile network time failed, attempting NTP...");
    if (syncWithNTP()) {
        _syncSource = SYNC_NTP;
        _initialized = true;
        log_i("[CLOCK] Clock initialized successfully from NTP: %s", 
              getFormattedDateTime().c_str());
        return true;
    }
    
    log_e("[CLOCK] Failed to initialize clock from both mobile network and NTP");
    return false;
}

bool ClockManager::enableWiFi() {
    log_i("[CLOCK] Enabling WiFi in STA mode...");
    
    String ssid = _configManager.getWiFiSSID();
    String password = _configManager.getWiFiPassword();
    
    if (ssid.length() == 0) {
        log_e("[CLOCK] WiFi SSID not configured");
        return false;
    }
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr);
    
    log_i("[CLOCK] WiFi connecting to: %s", ssid.c_str());
    
    return waitForWiFiConnection();
}

bool ClockManager::waitForWiFiConnection(unsigned long timeoutMs) {
    unsigned long startTime = millis();
    int dotCount = 0;
    
    while (millis() - startTime < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            log_i("[CLOCK] WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
            return true;
        }
        
        // Print progress dots
        if (millis() - startTime > (dotCount + 1) * 1000) {
            Serial.print(".");
            dotCount++;
        }
        
        delay(100);
    }
    
    log_w("[CLOCK] WiFi connection timeout after %lu ms", timeoutMs);
    return false;
}

void ClockManager::disableWiFi() {
    log_i("[CLOCK] Disabling WiFi...");
    WiFi.disconnect(true);  // Turn off WiFi radio
    WiFi.mode(WIFI_OFF);
}

bool ClockManager::syncWithNTP() {
    log_i("[CLOCK] Starting NTP synchronization...");
    
    // Enable WiFi
    if (!enableWiFi()) {
        log_w("[CLOCK] Failed to enable WiFi for NTP");
        disableWiFi();
        return false;
    }
    
    // Configure NTP time with detected timezone (DST will be applied dynamically)
    log_i("[CLOCK] Configuring NTP with servers: %s, %s, %s (TZ: %d sec)", 
          NTP_SERVER1, NTP_SERVER2, NTP_SERVER3, _tzOffsetSec);
    
    configTime(_tzOffsetSec, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
    
    // Wait for NTP time to be set (time() returns > 0 when set)
    unsigned long startTime = millis();
    const unsigned long NTP_TIMEOUT_MS = 30000; // 30 seconds to get NTP time
    
    while (millis() - startTime < NTP_TIMEOUT_MS) {
        time_t now = time(nullptr);
        
        if (now > 24 * 60 * 60) { // Sanity check: should be > 1970-01-02
            _epochTime = (unsigned long)now;
            _millisecondsAtSync = millis();
            _lastNtpSyncTime = millis();
            
            struct tm *timeinfo = gmtime(&now);
            log_i("[CLOCK] NTP time synced: %04d-%02d-%02d %02d:%02d:%02d (epoch: %lu, TZ offset: %d sec, DST: %d sec)",
                  timeinfo->tm_year + 1900,
                  timeinfo->tm_mon + 1,
                  timeinfo->tm_mday,
                  timeinfo->tm_hour,
                  timeinfo->tm_min,
                  timeinfo->tm_sec,
                  _epochTime,
                  _tzOffsetSec,
                  _configManager.getInt(ConfigManager::Param::DST_OFFSET));
            
            // Disable WiFi after successful sync
            disableWiFi();
            return true;
        }
        
        delay(500);
    }
    
    log_w("[CLOCK] NTP time sync timeout");
    disableWiFi();
    return false;
}

bool ClockManager::syncWithMobileNetwork() {
    log_i("[CLOCK] Syncing time from mobile network...");
    
    // Try to get time from network (epoch time)
    // TinyGsm provides getNetworkTime() but for some modems, we need to parse AT response
    // Most modems support +CCLK command which returns local time
    int year, month, day, hour, min, sec;
    float tzOffsetHours;  // Timezone in hours (as float)
    
    if (_modem.getNetworkTime(&year, &month, &day, &hour, &min, &sec, &tzOffsetHours)) {
        // Convert to Unix timestamp (approximate, doesn't account for timezone precisely)
        // This is a simplified conversion - for production, use a proper time library
        struct tm timeinfo = {0};
        timeinfo.tm_year = year - 1900;  // years since 1900
        timeinfo.tm_mon = month - 1;     // months since January (0-11)
        timeinfo.tm_mday = day;
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = min;
        timeinfo.tm_sec = sec;
        
        time_t epochTime = mktime(&timeinfo);
        
        if (epochTime > 0) {
            // Adjust for timezone offset returned by modem
            // tzOffsetHours is in hours, so multiply by 3600 to get seconds
            long tzOffsetSeconds = (long)(tzOffsetHours * 3600);
            
            // Update stored timezone for future NTP syncs
            _tzOffsetSec = (int)tzOffsetSeconds;
            
            _epochTime = epochTime;
            _millisecondsAtSync = millis();
            _lastNtpSyncTime = millis();
            
            log_i("[CLOCK] Mobile network time synced: %04d-%02d-%02d %02d:%02d:%02d TZ:%+.2f (epoch: %lu, offset: %d sec)",
                  year, month, day, hour, min, sec, tzOffsetHours, _epochTime, _tzOffsetSec);
            if (year != 0) {
                _syncSource = SYNC_MOBILE;
                _initialized = true;
                return true;
            } else {
                return false;
            }
                  
        }
    }
    
    log_w("[CLOCK] Failed to get time from mobile network");
    return false;
}

unsigned long ClockManager::calculateCurrentTime() {
    if (!_initialized) {
        return 0;
    }
    
    // Calculate elapsed time since sync
    unsigned long elapsedMs = millis() - _millisecondsAtSync;
    unsigned long elapsedSeconds = elapsedMs / 1000;
    
    // Base time depends on sync source
    unsigned long currentTime = _epochTime + elapsedSeconds;
    
    if (_syncSource == SYNC_NTP) {
        // NTP provides UTC time, apply timezone and DST offsets
        long localTime = (long)currentTime + _tzOffsetSec;
        
        // Apply DST offset dynamically (so changes take effect immediately)
        int dstOffsetSec = _configManager.getInt(ConfigManager::Param::DST_OFFSET);
        localTime += dstOffsetSec;
        
        return (unsigned long)localTime;
    } else if (_syncSource == SYNC_MOBILE) {
        // Mobile network provides local time (DST already included)
        // No need to apply timezone or DST offsets
        return currentTime;
    }
    
    return 0; // Should not reach here
}

unsigned long ClockManager::getCurrentTime() {
    if (!_initialized) {
        log_w("[CLOCK] Clock not initialized, returning 0");
        return 0;
    }
    
    return calculateCurrentTime();
}

String ClockManager::getFormattedDateTime() {
    unsigned long epochTime = getCurrentTime();
    
    if (epochTime == 0) {
        return "Not initialized";
    }
    
    time_t t = (time_t)epochTime;
    struct tm *timeinfo = gmtime(&t);  // Use gmtime to avoid double-applying TZ
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    
    return String(buffer);
}

String ClockManager::getFormattedTime() {
    unsigned long epochTime = getCurrentTime();
    
    if (epochTime == 0) {
        return "Not initialized";
    }
    
    time_t t = (time_t)epochTime;
    struct tm *timeinfo = gmtime(&t);  // Use gmtime to avoid double-applying TZ
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    
    return String(buffer);
}

String ClockManager::getFormattedDate() {
    unsigned long epochTime = getCurrentTime();
    
    if (epochTime == 0) {
        return "Not initialized";
    }
    
    time_t t = (time_t)epochTime;
    struct tm *timeinfo = gmtime(&t);  // Use gmtime to avoid double-applying TZ
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday);
    
    return String(buffer);
}

void ClockManager::check() {
    if (!_initialized) {
        return;
    }
    
    unsigned long now = millis();
    
    // Calculate the resync interval from config
    int ntpIntervalHours = _configManager.getInt(ConfigManager::Param::NTP_RESYNC_HOURS);
    unsigned long ntpResyncMs = (unsigned long)ntpIntervalHours * 60 * 60 * 1000;
    
    // Check if we need to resync with NTP
    if (now - _lastNtpSyncTime >= ntpResyncMs) {
        log_i("[CLOCK] Time for NTP resynchronization (interval: %d hours)...", ntpIntervalHours);
        
        if (syncWithNTP()) {
            _syncSource = SYNC_NTP;
            log_i("[CLOCK] Successfully resynchronized with NTP");
        } else {
            log_w("[CLOCK] NTP resync failed, attempting mobile network fallback...");
            if (syncWithMobileNetwork()) {
                _syncSource = SYNC_MOBILE;
                log_i("[CLOCK] Successfully resynchronized with mobile network");
            } else {
                log_w("[CLOCK] Failed to resynchronize time, continuing with local time");
            }
        }
    }
}
