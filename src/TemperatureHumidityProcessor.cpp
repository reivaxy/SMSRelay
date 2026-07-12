#include "TemperatureHumidityProcessor.h"
#include "ConfigManager.h"
#include "PhoneNumberManager.h"

TemperatureHumidityProcessor::TemperatureHumidityProcessor(SMSSender &sender, const String &targetNumber, uint8_t pin, ConfigManager &configManager, PhoneNumberManager &phoneNumberManager)
    : _sender(sender), _targetNumber(targetNumber), _dht(pin, DHT22), _pin(pin), _configManager(configManager), _phoneNumberManager(phoneNumberManager) {}

void TemperatureHumidityProcessor::init()
{
    log_i("Initializing DHT22 sensor on pin %d", _pin);
    _dht.begin();
    delay(2000);  // Give sensor time to stabilize
    _sensorReady = true;
    log_i("DHT22 sensor initialized");
}

void TemperatureHumidityProcessor::check()
{
    if (millis() - _lastCheck < CHECK_INTERVAL) return;
    _lastCheck = millis();
    readSensor();

    // Check thresholds 
    checkThresholds();
}

void TemperatureHumidityProcessor::readSensor()
{
    if (!_sensorReady) return;

    float temp = _dht.readTemperature();
    float hum  = _dht.readHumidity();

    // Check if any reads failed
    if (isnan(temp) || isnan(hum)) {
        log_w("Failed to read from DHT22 sensor");
        _sensorReady = false;
        return;
    }

    // Apply sensor offsets (calibration adjustments)
    float tempOffset = _configManager.getFloat(ConfigManager::Param::TEMP_OFFSET);
    float humidityOffset = _configManager.getFloat(ConfigManager::Param::HUMIDITY_OFFSET);
    
    _temperature = temp + tempOffset;
    _humidity    = hum + humidityOffset;
    log_i("DHT22 - Temp: %.1f°C (raw: %.1f°C, offset: %+.1f°C), Humidity: %.1f%% (raw: %.1f%%, offset: %+.1f%%)", 
          _temperature, temp, tempOffset, _humidity, hum, humidityOffset);
}

void TemperatureHumidityProcessor::checkThresholds()
{
    if (!_sensorReady) return;

    // Get current thresholds from manager
    float tempHighThreshold = _configManager.getFloat(ConfigManager::Param::TEMP_HIGH);
    float tempLowThreshold = _configManager.getFloat(ConfigManager::Param::TEMP_LOW);
    float humidityHighThreshold = _configManager.getFloat(ConfigManager::Param::HUMIDITY_HIGH);
    float humidityLowThreshold = _configManager.getFloat(ConfigManager::Param::HUMIDITY_LOW);

    // Check high temperature threshold
    if (_temperature > tempHighThreshold && !_tempHighAlertSent) {
        log_i("High temperature detected (%.1f°C), sending SMS...", _temperature);
        String msg = "ALERT: Temperature high (" + String(_temperature, 1) + "°C)";
        auto numbers = _phoneNumberManager.getAllNumbers();
        for (const auto &entry : numbers) {
            if (!_phoneNumberManager.isMuted(entry.number)) {
                if (_sender.send(entry.number, msg)) {
                    log_i("[OK] High temperature alert sent to %s", entry.number.c_str());
                } else {
                    log_i("[ERROR] Failed to send high temperature alert to %s", entry.number.c_str());
                }
            } else {
                log_i("[MUTED] High temperature alert not sent to %s (muted)", entry.number.c_str());
            }
        }
        _tempHighAlertSent = true;
        _tempLowAlertSent  = false;
    } else if (_temperature <= tempHighThreshold - 1.0f && _tempHighAlertSent) {
        log_i("Temperature normalized from high (%.1f°C)", _temperature);
        _tempHighAlertSent = false;
    }

    // Check low temperature threshold
    if (_temperature < tempLowThreshold && !_tempLowAlertSent) {
        log_i("Low temperature detected (%.1f°C), sending SMS...", _temperature);
        String msg = "ALERT: Temperature low (" + String(_temperature, 1) + "°C)";
        auto numbers = _phoneNumberManager.getAllNumbers();
        for (const auto &entry : numbers) {
            if (!_phoneNumberManager.isMuted(entry.number)) {
                if (_sender.send(entry.number, msg)) {
                    log_i("[OK] Low temperature alert sent to %s", entry.number.c_str());
                } else {
                    log_i("[ERROR] Failed to send low temperature alert to %s", entry.number.c_str());
                }
            } else {
                log_i("[MUTED] Low temperature alert not sent to %s (muted)", entry.number.c_str());
            }
        }
        _tempLowAlertSent = true;
        _tempHighAlertSent = false;
    } else if (_temperature >= tempLowThreshold + 1.0f && _tempLowAlertSent) {
        log_i("Temperature normalized from low (%.1f°C)", _temperature);
        _tempLowAlertSent = false;
    }

    // Check high humidity threshold
    if (_humidity > humidityHighThreshold && !_humidityHighAlertSent) {
        log_i("High humidity detected (%.1f%%), sending SMS...", _humidity);
        String msg = "ALERT: Humidity high (" + String(_humidity, 1) + "%)";
        auto numbers = _phoneNumberManager.getAllNumbers();
        for (const auto &entry : numbers) {
            if (!_phoneNumberManager.isMuted(entry.number)) {
                if (_sender.send(entry.number, msg)) {
                    log_i("[OK] High humidity alert sent to %s", entry.number.c_str());
                } else {
                    log_i("[ERROR] Failed to send high humidity alert to %s", entry.number.c_str());
                }
            } else {
                log_i("[MUTED] High humidity alert not sent to %s (muted)", entry.number.c_str());
            }
        }
        _humidityHighAlertSent = true;
        _humidityLowAlertSent  = false;
    } else if (_humidity <= humidityHighThreshold - 1.0f && _humidityHighAlertSent) {
        log_i("Humidity normalized from high (%.1f%%)", _humidity);
        _humidityHighAlertSent = false;
    }

    // Check low humidity threshold
    if (_humidity < humidityLowThreshold && !_humidityLowAlertSent) {
        log_i("Low humidity detected (%.1f%%), sending SMS...", _humidity);
        String msg = "ALERT: Humidity low (" + String(_humidity, 1) + "%)";
        auto numbers = _phoneNumberManager.getAllNumbers();
        for (const auto &entry : numbers) {
            if (!_phoneNumberManager.isMuted(entry.number)) {
                if (_sender.send(entry.number, msg)) {
                    log_i("[OK] Low humidity alert sent to %s", entry.number.c_str());
                } else {
                    log_i("[ERROR] Failed to send low humidity alert to %s", entry.number.c_str());
                }
            } else {
                log_i("[MUTED] Low humidity alert not sent to %s (muted)", entry.number.c_str());
            }
        }
        _humidityLowAlertSent = true;
        _humidityHighAlertSent = false;
    } else if (_humidity >= humidityLowThreshold + 1.0f && _humidityLowAlertSent) {
        log_i("Humidity normalized from low (%.1f%%)", _humidity);
        _humidityLowAlertSent = false;
    }
}

String TemperatureHumidityProcessor::getStatus() const
{
    if (!_sensorReady) {
        return "Sensor: Not ready";
    }
    return "Temp: " + String(_temperature, 1) + "C, Humidity: " + String(_humidity, 1) + "%";
}

void TemperatureHumidityProcessor::resetAlertFlags()
{
    log_i("Resetting temperature and humidity alert flags");
    _tempHighAlertSent = false;
    _tempLowAlertSent = false;
    _humidityHighAlertSent = false;
    _humidityLowAlertSent = false;
}
