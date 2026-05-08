#pragma once
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>

class SMSSender {
public:
    SMSSender(TinyGsm &modem, Stream &serialAT);

    // Send a UTF-8 message. Automatically selects IRA or UCS-2 path.
    bool send(const String &number, const String &text);

    // Encode a UTF-8 string to hex UCS-2 (4 uppercase hex chars per code unit).
    static String utf8ToUCS2Hex(const String &utf8);

    // Returns true if the UTF-8 string contains non-ASCII characters.
    static bool needsUCS2(const String &utf8);

private:
    // IRA path: plain text, 160 chars/part.
    bool sendLongSMS(const String &number, const String &text);
    // UCS-2 path: PDU mode (AT+CMGF=0), 70 chars/part.
    bool sendLongSMS_UCS2(const String &number, const String &hexText);
    bool sendSingleSMS_UCS2(const String &number, const String &hexChunk);
    // Build a SMS-SUBMIT PDU hex string for UCS-2 content.
    // pduLen is set to the byte count to pass to AT+CMGS (excluding SMSC info byte).
    static String buildSMSPDU(const String &number, const String &ucs2hex, int &pduLen);

    TinyGsm &_modem;
    Stream   &_serialAT;
};
