#pragma once
#include <Arduino.h>
#include "SMSSender.h"
#include "DHT.h"

// Forward declaration
class ConfigManager;

// Monitors DHT22 temperature and humidity sensor and sends alert SMS messages.
// Periodically checks sensor readings and sends alerts when thresholds are exceeded.
// Uses ConfigManager for dynamic, editable thresholds.
class TemperatureHumidityProcessor {
public:
    TemperatureHumidityProcessor(SMSSender &sender, const String &targetNumber, uint8_t pin, ConfigManager &configManager);

    // Initialize the sensor
    void init();

    // Call from loop() — checks sensor state and sends alerts
    void check();

    // Read current sensor values
    float getTemperature() const { return _temperature; }
    float getHumidity() const { return _humidity; }
    bool isSensorReady() const { return _sensorReady; }

    // Get formatted status string
    String getStatus() const;

    // Reset alert sent flags
    void resetAlertFlags();

private:
    void readSensor();
    void checkThresholds();

    SMSSender &_sender;
    String     _targetNumber;
    DHT        _dht;
    uint8_t    _pin;
    ConfigManager &_configManager;

    // Current readings
    float       _temperature = 0.0f;
    float       _humidity    = 0.0f;
    bool        _sensorReady = false;

    // Timing
    unsigned long _lastCheck              = 0;
    unsigned long _lastSensorRead         = 0;
    static constexpr unsigned long CHECK_INTERVAL        = 30000; 
    static constexpr unsigned long SENSOR_READ_INTERVAL  = 2000;  // Read sensor every 2 seconds

    // Alert tracking (only send alert once per threshold crossing)
    bool _tempHighAlertSent   = false;
    bool _tempLowAlertSent    = false;
    bool _humidityHighAlertSent = false;
    bool _humidityLowAlertSent  = false;
};
