#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Manages all configurable parameters (thresholds, settings, etc.) using ESP32 Preferences (NVS).
// Parameters are persisted to flash and can be modified via SMS commands.
// This is designed to be extensible for adding more parameters later.
class ConfigManager {
public:
    // Configuration parameter IDs - add new ones here for extensibility
    enum class Param {
        TEMP_HIGH,               // Temperature high threshold (float, Celsius)
        TEMP_LOW,                // Temperature low threshold (float, Celsius)
        HUMIDITY_HIGH,           // Humidity high threshold (float, percentage)
        HUMIDITY_LOW,            // Humidity low threshold (float, percentage)
        BAT_ADC_THRESHOLD,       // Battery power detection threshold (int, ADC value)
        BAT_ADC_NEAR_EMPTY,      // Battery near empty threshold (int, ADC value)
        POWER_ADC_THRESHOLD,     // Main power detection threshold (int, ADC value)
        TEMP_OFFSET,             // Temperature offset for sensor calibration (float, Celsius, can be negative)
        HUMIDITY_OFFSET,         // Humidity offset for sensor calibration (float, percentage, can be negative)
        ALERT_RESEND_DELAY_MINS, // Alert resend delay in minutes (int, default 5)
        WIFI_SSID,               // WiFi network SSID (string)
        WIFI_PASSWORD,           // WiFi network password (string)
        NTP_RESYNC_HOURS,        // NTP time resync interval in hours (int, default 4)
        DST_OFFSET               // Daylight Saving Time offset in seconds (int, 0 for winter, 3600 for +1hr, 7200 for +2hrs)
    };

    // Default values
    struct Defaults {
        static constexpr float TEMP_HIGH = 27.0f;
        static constexpr float TEMP_LOW = 24.0f;
        static constexpr float HUMIDITY_HIGH = 80.0f;
        static constexpr float HUMIDITY_LOW = 20.0f;
        static constexpr int BAT_ADC_THRESHOLD = 2330;
        static constexpr int BAT_ADC_NEAR_EMPTY = 1800;
        static constexpr int POWER_ADC_THRESHOLD = 2000;
        static constexpr float TEMP_OFFSET = 0.0f;
        static constexpr float HUMIDITY_OFFSET = 0.0f;
        static constexpr int ALERT_RESEND_DELAY_MINS = 5;
        static constexpr int NTP_RESYNC_HOURS = 4;
        static constexpr int DST_OFFSET = 0;  // 0 for winter time, 3600 for +1hr, 7200 for +2hrs
    };

    ConfigManager();
    ~ConfigManager();

    // Initialize preferences storage
    void init();

    // Get configuration value as float
    float getFloat(Param param);

    // Get configuration value as int
    int getInt(Param param);

    // Set configuration value
    void setFloat(Param param, float value);
    void setInt(Param param, int value);

    // Reset a parameter to its default value
    void resetToDefault(Param param);

    // Reset all parameters to defaults
    void resetAllToDefaults();

    // Get parameter name for display
    static String getParamName(Param param);

    // Get formatted info for a parameter (name and current value)
    String getParamInfo(Param param);

    // Get all configuration as formatted string
    String getAllParams();

    // Parse user input parameter name and return corresponding Param enum
    // Returns true if valid, false if unknown parameter
    static bool parseParamName(const String &userInput, Param &outParam);

    // Get list of valid parameter names for error messages
    static String getValidParamNames();

    // WiFi configuration (separate from parameter system)
    void setWiFiSSID(const String &ssid);
    String getWiFiSSID();
    void setWiFiPassword(const String &password);
    String getWiFiPassword();

    // String parameter access (for WiFi credentials and future string params)
    String getStringParam(Param param);
    void setStringParam(Param param, const String &value);

private:
    Preferences _prefs;
    
    // Get the key string for a parameter
    static String getParamKey(Param param);
    
    // Get the default value for a parameter
    static float getDefaultFloatValue(Param param);
    static int getDefaultIntValue(Param param);
};
