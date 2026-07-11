#pragma once
#include <Arduino.h>
#include "SMSSender.h"

// Forward declaration
class ConfigManager;

// Monitors GPIO00 ADC level and sends alert SMS messages when threshold is crossed.
// Uses ConfigManager for dynamic, editable thresholds.
class MainPowerCheck {
public:
    MainPowerCheck(SMSSender &sender, const String &targetNumber, ConfigManager &configManager);

    // Call from loop() — checks GPIO00 ADC level continuously.
    // Sends SMS alerts when level crosses threshold and logs to Serial every 500ms.
    void check();

    // Returns the averaged raw ADC value from GPIO00 (pin 36 on ESP32)
    static int readGPIO00ADC();

private:
    SMSSender    &_sender;
    String        _targetNumber;
    ConfigManager &_configManager;
    unsigned long _lastSerialLog     = 0;  // Track serial output timing (every 500ms)
    bool          _lowPowerAlertSent = false;
    bool          _normalPowerAlertSent = false;
    // Initialize to a value above threshold to get alert on startup
    // When main power goes down, module restarts. So if first value is below threshold we want the alert
    // so we consider previous value to be above threshold (4000) to trigger the alert on first check.
    int           _lastADCValue = 4000;  
};
