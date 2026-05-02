#pragma once
#include <Arduino.h>
#include "SMSReader.h"
#include "SMSSender.h"

// Handles incoming SMS from numbers other than SMS_TARGET.
// Forwards the header and body to the target number.
class SMSForwarder {
public:
    SMSForwarder(SMSSender &sender, const String &targetNumber);

    // Forwards the SMS to targetNumber. Returns true on success.
    bool forward(const ReceivedSMS &sms);

private:
    SMSSender &_sender;
    String     _targetNumber;
};
