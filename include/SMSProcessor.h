#pragma once
#include <Arduino.h>
#include "SMSReader.h"
#include "SMSSender.h"

// Handles command SMS received from SMS_TARGET.
// Supported commands: "FOR:<number> <message>", "STATUS"
class SMSProcessor {
public:
    SMSProcessor(SMSSender &sender, const String &targetNumber);

    // Processes a command SMS. The original SMS is always considered handled
    // (caller should delete it regardless of individual command success).
    void process(const ReceivedSMS &sms);

    // Returns the averaged raw ADC value from BOARD_BAT_ADC_PIN (0 if pin not defined).
    static int readBatADC();

    // Returns true if the device is powered by battery (via ADC reading).
    static bool isPoweredByBattery();

    static constexpr int BAT_ADC_THRESHOLD = 2300;

private:
    void handleForCommand(const String &rest);
    void handleStatusCommand();

    SMSSender &_sender;
    String     _targetNumber;
};
