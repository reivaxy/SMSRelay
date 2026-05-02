#pragma once
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>

struct ReceivedSMS {
    String number;
    String text;
    String textRaw;   // original hex UCS-2 body as received from modem
    String timestamp;
    int index;
};

class SMSReader {
public:
    SMSReader(TinyGsm &modem, Stream &serialAT);

    // Reads the next unread SMS. Returns true if one was found and fills sms.
    // Switches charset to UCS2 on entry; restores IRA before returning.
    bool readNext(ReceivedSMS &sms);

    // Deletes the SMS at the given modem index.
    void deleteMessage(int index);

    static bool isHexUCS2(const String &s);
    static String decodeUCS2Hex(const String &s);

private:
    TinyGsm &_modem;
    Stream   &_serialAT;
};
