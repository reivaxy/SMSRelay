#pragma once
#include <Arduino.h>
#include "SMSSender.h"

// Forward declaration
class ConfigManager;

// Monitors battery ADC level and sends alert SMS messages.
// Uses ConfigManager for dynamic, editable thresholds.
class BatteryProcessor {
public:
    BatteryProcessor(SMSSender &sender, const String &targetNumber, ConfigManager &configManager);

    // Call from loop() — checks battery state every minute and sends alerts.
    void check();

    // Returns the averaged raw ADC value from BOARD_BAT_ADC_PIN (0 if pin not defined).
    static int readBatADC();

private:
    SMSSender    &_sender;
    String        _targetNumber;
    ConfigManager &_configManager;
    unsigned long _lastCheck         = 0;
    bool          _batteryAlertSent  = false;
    bool          _usbAlertSent      = false;
    bool          _nearEmptyAlertSent = false;
    int           _lastAlertADC      = 0;
};
