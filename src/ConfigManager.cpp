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
    _prefs.putInt("BAT_ADC_THRESHOLD", Defaults::BAT_ADC_THRESHOLD);
    _prefs.putInt("BAT_ADC_NEAR_EMPTY", Defaults::BAT_ADC_NEAR_EMPTY);
    _prefs.putInt("POWER_ADC_THRESHOLD", Defaults::POWER_ADC_THRESHOLD);
    log_i("[CONFIG] All parameters reset to defaults");
}

String ConfigManager::getParamKey(Param param) {
    switch (param) {
        case Param::TEMP_HIGH:          return "TEMP_HIGH";
        case Param::TEMP_LOW:           return "TEMP_LOW";
        case Param::HUMIDITY_HIGH:      return "HUMIDITY_HIGH";
        case Param::HUMIDITY_LOW:       return "HUMIDITY_LOW";
        case Param::BAT_ADC_THRESHOLD:  return "BAT_ADC_THRESHOLD";
        case Param::BAT_ADC_NEAR_EMPTY: return "BAT_ADC_NEAR_EMPTY";
        case Param::POWER_ADC_THRESHOLD: return "POWER_ADC_THRESHOLD";
    }
    return "UNKNOWN";
}

String ConfigManager::getParamName(Param param) {
    switch (param) {
        case Param::TEMP_HIGH:          return "Temp High";
        case Param::TEMP_LOW:           return "Temp Low";
        case Param::HUMIDITY_HIGH:      return "Humidity High";
        case Param::HUMIDITY_LOW:       return "Humidity Low";
        case Param::BAT_ADC_THRESHOLD:  return "Battery Threshold";
        case Param::BAT_ADC_NEAR_EMPTY: return "Battery Near Empty";
        case Param::POWER_ADC_THRESHOLD: return "Power Threshold";
    }
    return "Unknown";
}

float ConfigManager::getDefaultFloatValue(Param param) {
    switch (param) {
        case Param::TEMP_HIGH:     return Defaults::TEMP_HIGH;
        case Param::TEMP_LOW:      return Defaults::TEMP_LOW;
        case Param::HUMIDITY_HIGH: return Defaults::HUMIDITY_HIGH;
        case Param::HUMIDITY_LOW:  return Defaults::HUMIDITY_LOW;
        default:                   return 0.0f;
    }
}

int ConfigManager::getDefaultIntValue(Param param) {
    switch (param) {
        case Param::BAT_ADC_THRESHOLD:   return Defaults::BAT_ADC_THRESHOLD;
        case Param::BAT_ADC_NEAR_EMPTY:  return Defaults::BAT_ADC_NEAR_EMPTY;
        case Param::POWER_ADC_THRESHOLD: return Defaults::POWER_ADC_THRESHOLD;
        default:                         return 0;
    }
}

String ConfigManager::getParamInfo(Param param) {
    String name = getParamName(param);
    
    // Float parameters
    if (param == Param::TEMP_HIGH || param == Param::TEMP_LOW ||
        param == Param::HUMIDITY_HIGH || param == Param::HUMIDITY_LOW) {
        float val = getFloat(param);
        String unit = (param == Param::TEMP_HIGH || param == Param::TEMP_LOW) ? "°C" : "%";
        return name + ": " + String(val, 1) + unit;
    }
    
    // Int parameters
    int val = getInt(param);
    return name + ": " + String(val);
}

String ConfigManager::getAllParams() {
    String msg = "Parameters:\n";
    msg += getParamInfo(Param::TEMP_HIGH) + "\n";
    msg += getParamInfo(Param::TEMP_LOW) + "\n";
    msg += getParamInfo(Param::HUMIDITY_HIGH) + "\n";
    msg += getParamInfo(Param::HUMIDITY_LOW) + "\n";
    msg += getParamInfo(Param::BAT_ADC_THRESHOLD) + "\n";
    msg += getParamInfo(Param::BAT_ADC_NEAR_EMPTY) + "\n";
    msg += getParamInfo(Param::POWER_ADC_THRESHOLD);
    return msg;
}
