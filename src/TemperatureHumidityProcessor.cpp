#include "TemperatureHumidityProcessor.h"

TemperatureHumidityProcessor::TemperatureHumidityProcessor(SMSSender &sender, const String &targetNumber, uint8_t pin)
    : _sender(sender), _targetNumber(targetNumber), _dht(pin, DHT22), _pin(pin) {}

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
    log_i("Temperature: %.1f°C, Humidity: %.1f%%", _temperature, _humidity);
    // Check thresholds periodically
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

    _temperature = temp;
    _humidity    = hum;
    log_i("DHT22 - Temp: %.1f°C, Humidity: %.1f%%", _temperature, _humidity);
}

void TemperatureHumidityProcessor::checkThresholds()
{
    if (!_sensorReady) return;

    // Check high temperature threshold
    if (_temperature > TEMP_HIGH_THRESHOLD && !_tempHighAlertSent) {
        log_i("High temperature detected (%.1f°C), sending SMS...", _temperature);
        String msg = "ALERT: Temperature high (" + String(_temperature, 1) + "°C)";
        if (_sender.send(_targetNumber, msg)) {
            log_i("[OK] High temperature alert sent");
            _tempHighAlertSent = true;
            _tempLowAlertSent  = false;
        } else {
            log_i("[ERROR] Failed to send high temperature alert");
        }
    } else if (_temperature <= TEMP_HIGH_THRESHOLD - 1.0f && _tempHighAlertSent) {
        log_i("Temperature normalized from high (%.1f°C)", _temperature);
        _tempHighAlertSent = false;
    }

    // Check low temperature threshold
    if (_temperature < TEMP_LOW_THRESHOLD && !_tempLowAlertSent) {
        log_i("Low temperature detected (%.1f°C), sending SMS...", _temperature);
        String msg = "ALERT: Temperature low (" + String(_temperature, 1) + "°C)";
        if (_sender.send(_targetNumber, msg)) {
            log_i("[OK] Low temperature alert sent");
            _tempLowAlertSent = true;
            _tempHighAlertSent = false;
        } else {
            log_i("[ERROR] Failed to send low temperature alert");
        }
    } else if (_temperature >= TEMP_LOW_THRESHOLD + 1.0f && _tempLowAlertSent) {
        log_i("Temperature normalized from low (%.1f°C)", _temperature);
        _tempLowAlertSent = false;
    }

    // Check high humidity threshold
    if (_humidity > HUMIDITY_HIGH_THRESHOLD && !_humidityHighAlertSent) {
        log_i("High humidity detected (%.1f%%), sending SMS...", _humidity);
        String msg = "ALERT: Humidity high (" + String(_humidity, 1) + "%)";
        if (_sender.send(_targetNumber, msg)) {
            log_i("[OK] High humidity alert sent");
            _humidityHighAlertSent = true;
            _humidityLowAlertSent  = false;
        } else {
            log_i("[ERROR] Failed to send high humidity alert");
        }
    } else if (_humidity <= HUMIDITY_HIGH_THRESHOLD - 1.0f && _humidityHighAlertSent) {
        log_i("Humidity normalized from high (%.1f%%)", _humidity);
        _humidityHighAlertSent = false;
    }

    // Check low humidity threshold
    if (_humidity < HUMIDITY_LOW_THRESHOLD && !_humidityLowAlertSent) {
        log_i("Low humidity detected (%.1f%%), sending SMS...", _humidity);
        String msg = "ALERT: Humidity low (" + String(_humidity, 1) + "%)";
        if (_sender.send(_targetNumber, msg)) {
            log_i("[OK] Low humidity alert sent");
            _humidityLowAlertSent = true;
            _humidityHighAlertSent = false;
        } else {
            log_i("[ERROR] Failed to send low humidity alert");
        }
    } else if (_humidity >= HUMIDITY_LOW_THRESHOLD + 1.0f && _humidityLowAlertSent) {
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
