#pragma once
#include <Arduino.h>
#include "SMSSender.h"

// Forward declarations
class ConfigManager;
class PhoneNumberManager;
class AlertManager;

// Monitors GPIO00 ADC level and sends alert SMS messages when threshold is crossed.
// Uses ConfigManager for dynamic, editable thresholds.
// Sends alerts to all authorized phone numbers.
class MainPowerCheck {
public:
    MainPowerCheck(SMSSender &sender, const String &targetNumber, ConfigManager &configManager, PhoneNumberManager &phoneNumberManager, AlertManager &alertManager);

    // Call from loop() — checks GPIO00 ADC level continuously.
    // Sends SMS alerts when level crosses threshold and logs to Serial every 500ms.
    void check();

    // Returns the averaged raw ADC value from GPIO00 (pin 36 on ESP32)
    static int readGPIO00ADC();

    // Reset alert sent flags
    void resetAlertFlags();

private:
    SMSSender         &_sender;
    String             _targetNumber;
    ConfigManager     &_configManager;
    PhoneNumberManager &_phoneNumberManager;
    AlertManager      &_alertManager;
    unsigned long      _lastSerialLog     = 0;  // Track serial output timing (every 500ms)
    bool               _lowPowerAlertSent = false;
    bool               _normalPowerAlertSent = false;
    String             _lowPowerAlertCode = "";  // Track the alert code
    // Initialize to a value above threshold to get alert on startup
    // When main power goes down, module restarts. So if first value is below threshold we want the alert
    // so we consider previous value to be above threshold (4000) to trigger the alert on first check.
    int           _lastADCValue = 4000;  
};
