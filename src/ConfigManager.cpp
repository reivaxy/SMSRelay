#include "ConfigManager.h"
#include "utilities.h"

ConfigManager::ConfigManager() {}

ConfigManager::~ConfigManager() {
    if (_prefs.isKey("initialized")) {
        _prefs.end();
    }
}

void ConfigManager::init() {
    log_i("[CONFIG] Initializing ConfigManager");
    if (!_prefs.begin("config", false)) {
        log_e("[CONFIG] Failed to initialize Preferences");
        return;
    }

    // Initialize all parameters to defaults if they don't exist
    if (!_prefs.isKey("initialized")) {
        log_i("[CONFIG] First initialization, setting all defaults");
        resetAllToDefaults();
        _prefs.putBool("initialized", true);
    } else {
        // Ensure all parameters exist (for upgrades from previous versions)
        log_i("[CONFIG] Checking for missing parameters");
        if (!_prefs.isKey("TEMP_HIGH")) _prefs.putFloat("TEMP_HIGH", Defaults::TEMP_HIGH);
        if (!_prefs.isKey("TEMP_LOW")) _prefs.putFloat("TEMP_LOW", Defaults::TEMP_LOW);
        if (!_prefs.isKey("HUMIDITY_HIGH")) _prefs.putFloat("HUMIDITY_HIGH", Defaults::HUMIDITY_HIGH);
        if (!_prefs.isKey("HUMIDITY_LOW")) _prefs.putFloat("HUMIDITY_LOW", Defaults::HUMIDITY_LOW);
        if (!_prefs.isKey("BAT_THRESHOLD")) _prefs.putInt("BAT_THRESHOLD", Defaults::BAT_ADC_THRESHOLD);
        if (!_prefs.isKey("BAT_NEAR_EMPTY")) _prefs.putInt("BAT_NEAR_EMPTY", Defaults::BAT_ADC_NEAR_EMPTY);
        if (!_prefs.isKey("POWER_THRESHOLD")) _prefs.putInt("POWER_THRESHOLD", Defaults::POWER_ADC_THRESHOLD);
        if (!_prefs.isKey("TEMP_OFFSET")) _prefs.putFloat("TEMP_OFFSET", Defaults::TEMP_OFFSET);
        if (!_prefs.isKey("HUMIDITY_OFFSET")) _prefs.putFloat("HUMIDITY_OFFSET", Defaults::HUMIDITY_OFFSET);
        if (!_prefs.isKey("RESEND_MINS")) _prefs.putInt("RESEND_MINS", Defaults::ALERT_RESEND_DELAY_MINS);
        if (!_prefs.isKey("NTP_RESYNC_HRS")) _prefs.putInt("NTP_RESYNC_HRS", Defaults::NTP_RESYNC_HOURS);
        if (!_prefs.isKey("DST_OFFSET")) _prefs.putInt("DST_OFFSET", Defaults::DST_OFFSET);
        
        // Clean up any old/stray keys that might interfere
        const char* oldKeys[] = {"ALERT_RESEND_DELAY_MINS", "ALERT_RESEND_MIN", "BAT_ADC_THRESHOLD", "BAT_ADC_NEAR_EMPTY", "POWER_ADC_THRESHOLD"};
        for (const auto& oldKey : oldKeys) {
            if (_prefs.isKey(oldKey)) {
                log_i("[CONFIG] Removing stray old key: %s", oldKey);
                _prefs.remove(oldKey);
            }
        }
        log_i("[CONFIG] Parameter check complete");
    }
    log_i("[CONFIG] ConfigManager ready");
}

float ConfigManager::getFloat(Param param) {
    String key = getParamKey(param);
    float defaultVal = getDefaultFloatValue(param);
    float val = _prefs.getFloat(key.c_str(), defaultVal);
    log_d("[CONFIG] getFloat(%s) = %.2f", key.c_str(), val);
    return val;
}

int ConfigManager::getInt(Param param) {
    String key = getParamKey(param);
    int defaultVal = getDefaultIntValue(param);
    int val = _prefs.getInt(key.c_str(), defaultVal);
    log_d("[CONFIG] getInt(%s) = %d", key.c_str(), val);
    return val;
}

void ConfigManager::setFloat(Param param, float value) {
    String key = getParamKey(param);
    _prefs.putFloat(key.c_str(), value);
    log_i("[CONFIG] setFloat(%s) = %.2f", key.c_str(), value);
}

void ConfigManager::setInt(Param param, int value) {
    String key = getParamKey(param);
    _prefs.putInt(key.c_str(), value);
    log_i("[CONFIG] setInt(%s) = %d", key.c_str(), value);
}

void ConfigManager::resetToDefault(Param param) {
    String key = getParamKey(param);
    _prefs.remove(key.c_str());
    log_i("[CONFIG] resetToDefault(%s)", key.c_str());
}

void ConfigManager::resetAllToDefaults() {
    _prefs.putFloat("TEMP_HIGH", Defaults::TEMP_HIGH);
    _prefs.putFloat("TEMP_LOW", Defaults::TEMP_LOW);
    _prefs.putFloat("HUMIDITY_HIGH", Defaults::HUMIDITY_HIGH);
    _prefs.putFloat("HUMIDITY_LOW", Defaults::HUMIDITY_LOW);
    _prefs.putInt("BAT_THRESHOLD", Defaults::BAT_ADC_THRESHOLD);
    _prefs.putInt("BAT_NEAR_EMPTY", Defaults::BAT_ADC_NEAR_EMPTY);
    _prefs.putInt("POWER_THRESHOLD", Defaults::POWER_ADC_THRESHOLD);
    _prefs.putFloat("TEMP_OFFSET", Defaults::TEMP_OFFSET);
    _prefs.putFloat("HUMIDITY_OFFSET", Defaults::HUMIDITY_OFFSET);
    _prefs.putInt("RESEND_MINS", Defaults::ALERT_RESEND_DELAY_MINS);
    _prefs.putInt("NTP_RESYNC_HRS", Defaults::NTP_RESYNC_HOURS);
    _prefs.putInt("DST_OFFSET", Defaults::DST_OFFSET);
    log_i("[CONFIG] All parameters reset to defaults");
}

String ConfigManager::getParamKey(Param param) {
    switch (param) {
        case Param::TEMP_HIGH:               return "TEMP_HIGH";
        case Param::TEMP_LOW:                return "TEMP_LOW";
        case Param::HUMIDITY_HIGH:           return "HUMIDITY_HIGH";
        case Param::HUMIDITY_LOW:            return "HUMIDITY_LOW";
        case Param::BAT_ADC_THRESHOLD:       return "BAT_THRESHOLD";
        case Param::BAT_ADC_NEAR_EMPTY:      return "BAT_NEAR_EMPTY";
        case Param::POWER_ADC_THRESHOLD:     return "POWER_THRESHOLD";
        case Param::TEMP_OFFSET:             return "TEMP_OFFSET";
        case Param::HUMIDITY_OFFSET:         return "HUMIDITY_OFFSET";
        case Param::ALERT_RESEND_DELAY_MINS: return "RESEND_MINS";
        case Param::WIFI_SSID:               return "WIFI_SSID";
        case Param::WIFI_PASSWORD:           return "WIFI_PASS";
        case Param::NTP_RESYNC_HOURS: return "NTP_RESYNC_HRS";
        case Param::DST_OFFSET:      return "DST_OFFSET";
    }
    return "UNKNOWN";
}

bool ConfigManager::parseParamName(const String &userInput, Param &outParam) {
    String input = userInput;
    input.toUpperCase();
    
    // Map user input to Param enum
    if (input == "TEMP_HIGH") { outParam = Param::TEMP_HIGH; return true; }
    else if (input == "TEMP_LOW") { outParam = Param::TEMP_LOW; return true; }
    else if (input == "HUMIDITY_HIGH") { outParam = Param::HUMIDITY_HIGH; return true; }
    else if (input == "HUMIDITY_LOW") { outParam = Param::HUMIDITY_LOW; return true; }
    else if (input == "BAT_THRESHOLD") { outParam = Param::BAT_ADC_THRESHOLD; return true; }
    else if (input == "BAT_NEAR_EMPTY") { outParam = Param::BAT_ADC_NEAR_EMPTY; return true; }
    else if (input == "POWER_THRESHOLD") { outParam = Param::POWER_ADC_THRESHOLD; return true; }
    else if (input == "TEMP_OFFSET") { outParam = Param::TEMP_OFFSET; return true; }
    else if (input == "HUMIDITY_OFFSET") { outParam = Param::HUMIDITY_OFFSET; return true; }
    else if (input == "RESEND_MINS") { outParam = Param::ALERT_RESEND_DELAY_MINS; return true; }
    else if (input == "WIFI_SSID") { outParam = Param::WIFI_SSID; return true; }
    else if (input == "WIFI_PWD" || input == "WIFI_PASSWORD") { outParam = Param::WIFI_PASSWORD; return true; }
    else if (input == "NTP_RESYNC_HRS" || input == "NTP_INTERVAL" || input == "NTP_RESYNC_INTERVAL_HOURS") { outParam = Param::NTP_RESYNC_HOURS; return true; }
    else if (input == "DST_OFFSET" || input == "DST_OFFSET_SECONDS") { outParam = Param::DST_OFFSET; return true; }
    
    return false;
}

String ConfigManager::getValidParamNames() {
    return "TEMP_HIGH, TEMP_LOW, TEMP_OFFSET, HUMIDITY_HIGH, HUMIDITY_LOW, HUMIDITY_OFFSET, BAT_THRESHOLD, BAT_NEAR_EMPTY, POWER_THRESHOLD, RESEND_MINS, WIFI_SSID, WIFI_PWD, NTP_RESYNC_HRS, DST_OFFSET";
}

String ConfigManager::getParamName(Param param) {
    switch (param) {
        case Param::TEMP_HIGH:               return "Temp High";
        case Param::TEMP_LOW:                return "Temp Low";
        case Param::HUMIDITY_HIGH:           return "Humidity High";
        case Param::HUMIDITY_LOW:            return "Humidity Low";
        case Param::BAT_ADC_THRESHOLD:       return "Battery Threshold";
        case Param::BAT_ADC_NEAR_EMPTY:      return "Battery Near Empty";
        case Param::POWER_ADC_THRESHOLD:     return "Power Threshold";
        case Param::TEMP_OFFSET:             return "Temp Offset";
        case Param::HUMIDITY_OFFSET:         return "Humidity Offset";
        case Param::ALERT_RESEND_DELAY_MINS: return "Alert Resend Delay (min)";
        case Param::WIFI_SSID:               return "WiFi SSID";
        case Param::WIFI_PASSWORD:           return "WiFi Password";
        case Param::NTP_RESYNC_HOURS: return "NTP Resync Interval (hours)";
        case Param::DST_OFFSET:      return "DST Offset (seconds)";
    }
    return "Unknown";
}

float ConfigManager::getDefaultFloatValue(Param param) {
    switch (param) {
        case Param::TEMP_HIGH:     return Defaults::TEMP_HIGH;
        case Param::TEMP_LOW:      return Defaults::TEMP_LOW;
        case Param::HUMIDITY_HIGH: return Defaults::HUMIDITY_HIGH;
        case Param::HUMIDITY_LOW:  return Defaults::HUMIDITY_LOW;
        case Param::TEMP_OFFSET:   return Defaults::TEMP_OFFSET;
        case Param::HUMIDITY_OFFSET: return Defaults::HUMIDITY_OFFSET;
        default:                   return 0.0f;
    }
}

int ConfigManager::getDefaultIntValue(Param param) {
    switch (param) {
        case Param::BAT_ADC_THRESHOLD:         return Defaults::BAT_ADC_THRESHOLD;
        case Param::BAT_ADC_NEAR_EMPTY:        return Defaults::BAT_ADC_NEAR_EMPTY;
        case Param::POWER_ADC_THRESHOLD:       return Defaults::POWER_ADC_THRESHOLD;
        case Param::ALERT_RESEND_DELAY_MINS:   return Defaults::ALERT_RESEND_DELAY_MINS;
        case Param::NTP_RESYNC_HOURS: return Defaults::NTP_RESYNC_HOURS;
        case Param::DST_OFFSET:        return Defaults::DST_OFFSET;
        default:                               return 0;
    }
}

String ConfigManager::getParamInfo(Param param) {
    String name = getParamName(param);
    
    // Float parameters
    if (param == Param::TEMP_HIGH || param == Param::TEMP_LOW ||
        param == Param::HUMIDITY_HIGH || param == Param::HUMIDITY_LOW ||
        param == Param::TEMP_OFFSET || param == Param::HUMIDITY_OFFSET) {
        float val = getFloat(param);
        String unit = (param == Param::TEMP_HIGH || param == Param::TEMP_LOW || param == Param::TEMP_OFFSET) ? "°C" : "%";
        return name + ": " + String(val, 1) + unit;
    }
    
    // String parameters
    if (param == Param::WIFI_SSID || param == Param::WIFI_PASSWORD) {
        String val = getStringParam(param);
        if (param == Param::WIFI_PASSWORD) {
            return name + ": " + (val.length() > 0 ? "***" : "(not set)");
        } else {
            return name + ": " + (val.length() > 0 ? val : "(not set)");
        }
    }
    
    // Int parameters
    int val = getInt(param);
    if (param == Param::ALERT_RESEND_DELAY_MINS) {
        return name + ": " + String(val) + " min";
    }
    if (param == Param::NTP_RESYNC_HOURS) {
        return name + ": " + String(val) + " hours";
    }
    if (param == Param::DST_OFFSET) {
        float hours = val / 3600.0f;
        return name + ": " + String(hours, 1) + " hours";
    }
    return name + ": " + String(val);
}

String ConfigManager::getAllParams() {
    String msg = "Parameters:\n";
    msg += getParamInfo(Param::TEMP_HIGH) + "\n";
    msg += getParamInfo(Param::TEMP_LOW) + "\n";
    msg += getParamInfo(Param::TEMP_OFFSET) + "\n";
    msg += getParamInfo(Param::HUMIDITY_HIGH) + "\n";
    msg += getParamInfo(Param::HUMIDITY_LOW) + "\n";
    msg += getParamInfo(Param::HUMIDITY_OFFSET) + "\n";
    msg += getParamInfo(Param::BAT_ADC_THRESHOLD) + "\n";
    msg += getParamInfo(Param::BAT_ADC_NEAR_EMPTY) + "\n";
    msg += getParamInfo(Param::POWER_ADC_THRESHOLD) + "\n";
    msg += getParamInfo(Param::ALERT_RESEND_DELAY_MINS) + "\n";
    msg += getParamInfo(Param::WIFI_SSID) + "\n";
    msg += getParamInfo(Param::WIFI_PASSWORD) + "\n";
    msg += getParamInfo(Param::NTP_RESYNC_HOURS) + "\n";
    msg += getParamInfo(Param::DST_OFFSET);
    return msg;
}

void ConfigManager::setWiFiSSID(const String &ssid) {
    _prefs.putString("WIFI_SSID", ssid);
    log_i("[CONFIG] WiFi SSID set to: %s", ssid.c_str());
}

String ConfigManager::getWiFiSSID() {
    String ssid = _prefs.getString("WIFI_SSID", "");
    log_d("[CONFIG] WiFi SSID retrieved: %s", ssid.length() > 0 ? "***" : "(empty)");
    return ssid;
}

void ConfigManager::setWiFiPassword(const String &password) {
    _prefs.putString("WIFI_PASS", password);
    log_i("[CONFIG] WiFi password set (length: %d)", password.length());
}

String ConfigManager::getWiFiPassword() {
    String password = _prefs.getString("WIFI_PASS", "");
    log_d("[CONFIG] WiFi password retrieved (length: %d)", password.length());
    return password;
}

String ConfigManager::getStringParam(Param param) {
    String key = getParamKey(param);
    String val = _prefs.getString(key.c_str(), "");
    log_d("[CONFIG] getString(%s) = %s", key.c_str(), val.length() > 0 ? "***" : "(empty)");
    return val;
}

void ConfigManager::setStringParam(Param param, const String &value) {
    String key = getParamKey(param);
    _prefs.putString(key.c_str(), value);
    log_i("[CONFIG] setString(%s) = %s", key.c_str(), value.length() > 0 ? "***" : "(empty)");
}
