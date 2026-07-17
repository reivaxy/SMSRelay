#include "utilities.h"
#include "secret.h"
#include <TinyGsmClient.h>
#include "ClockManager.h"

ClockManager::ClockManager(TinyGsm &modem)
    : _modem(modem), _initialized(false), _epochTime(0), 
      _millisecondsAtSync(0), _lastSyncTime(0) {
    log_i("[CLOCK] ClockManager initialized");
}

ClockManager::~ClockManager() {
}

bool ClockManager::init() {
    log_i("[CLOCK] Initializing clock from network time...");
    if (syncWithNetwork()) {
        _initialized = true;
        log_i("[CLOCK] Clock initialized successfully with network time: %s", 
              getFormattedDateTime().c_str());
        return true;
    }
    log_e("[CLOCK] Failed to initialize clock from network");
    return false;
}

bool ClockManager::syncWithNetwork() {
    log_i("[CLOCK] Syncing time from mobile network...");
    
    // Try to get time from network (epoch time)
    // TinyGsm provides getNetworkTime() but for some modems, we need to parse AT response
    // Most modems support +CCLK command which returns local time
    int year, month, day, hour, min, sec;
    float tzquarter;  // Timezone in quarter hours (as float)
    
    if (_modem.getNetworkTime(&year, &month, &day, &hour, &min, &sec, &tzquarter)) {
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
            _epochTime = (unsigned long)epochTime;
            _millisecondsAtSync = millis();
            _lastSyncTime = millis();
            
            log_i("[CLOCK] Network time synced: %04d-%02d-%02d %02d:%02d:%02d (epoch: %lu)",
                  year, month, day, hour, min, sec, _epochTime);
            return true;
        }
    }
    
    log_w("[CLOCK] Failed to get time from network");
    return false;
}

unsigned long ClockManager::calculateCurrentTime() {
    if (!_initialized) {
        return 0;
    }
    
    unsigned long elapsedMs = millis() - _millisecondsAtSync;
    unsigned long elapsedSeconds = elapsedMs / 1000;
    
    return _epochTime + elapsedSeconds;
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
    struct tm *timeinfo = localtime(&t);
    
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
    struct tm *timeinfo = localtime(&t);
    
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
    struct tm *timeinfo = localtime(&t);
    
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
    
    // Check if we need to resync with network (every 5 minutes)
    if (now - _lastSyncTime >= RESYNC_INTERVAL_MS) {
        log_i("[CLOCK] Resynchronizing with network time...");
        if (syncWithNetwork()) {
            log_i("[CLOCK] Successfully resynchronized with network");
        } else {
            log_w("[CLOCK] Failed to resynchronize with network, continuing with local time");
            // Continue with local time calculation - it will drift slightly but still functional
        }
    }
}
