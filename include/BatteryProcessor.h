#pragma once
#include <Arduino.h>
#include "SMSSender.h"

// Monitors battery ADC level and sends alert SMS messages.
class BatteryProcessor {
public:
    BatteryProcessor(SMSSender &sender, const String &targetNumber);

    // Call from loop() — checks battery state every minute and sends alerts.
    void check();

    // Returns the averaged raw ADC value from BOARD_BAT_ADC_PIN (0 if pin not defined).
    static int readBatADC();

    static constexpr int BAT_ADC_THRESHOLD            = 2330; // above: usb power, below: battery power
    static constexpr int BAT_ADC_NEAR_EMPTY_THRESHOLD = 1800;

private:
    SMSSender    &_sender;
    String        _targetNumber;
    unsigned long _lastCheck         = 0;
    bool          _batteryAlertSent  = false;
    bool          _usbAlertSent      = false;
    bool          _nearEmptyAlertSent = false;
    int           _lastAlertADC      = 0;
};
