#pragma once
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>

class SMSSender {
public:
    explicit SMSSender(TinyGsm &modem);

    // Send a plain-text message, splitting into 160-char parts if needed.
    bool send(const String &number, const String &text);

    // Encode a UTF-8 string to hex UCS-2 (4 uppercase hex chars per code unit).
    static String utf8ToUCS2Hex(const String &utf8);

private:
    bool sendLongSMS(const String &number, const String &text);
    bool sendLongSMS_UCS2(const String &number, const String &hexText);

    TinyGsm &_modem;
};
