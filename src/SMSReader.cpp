#include "SMSReader.h"

SMSReader::SMSReader(TinyGsm &modem, Stream &serialAT)
    : _modem(modem), _serialAT(serialAT) {}

bool SMSReader::isHexUCS2(const String &s)
{
    if (s.length() == 0 || s.length() % 4 != 0) return false;
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

String SMSReader::decodeUCS2Hex(const String &s)
{
    if (!isHexUCS2(s)) return s;
    String result = "";
    for (int i = 0; i + 3 < (int)s.length(); i += 4) {
        uint16_t cp = 0;
        for (int j = 0; j < 4; j++) {
            char c = s[i + j];
            uint8_t n = (c >= '0' && c <= '9') ? (uint8_t)(c - '0') :
                        (c >= 'A' && c <= 'F') ? (uint8_t)(c - 'A' + 10) :
                                                  (uint8_t)(c - 'a' + 10);
            cp = (cp << 4) | n;
        }
        if (cp < 0x80) {
            result += (char)cp;
        } else if (cp < 0x800) {
            result += (char)(0xC0 | (cp >> 6));
            result += (char)(0x80 | (cp & 0x3F));
        } else {
            result += (char)(0xE0 | (cp >> 12));
            result += (char)(0x80 | ((cp >> 6) & 0x3F));
            result += (char)(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

bool SMSReader::readNext(ReceivedSMS &sms)
{
    _modem.sendAT("+CSCS=\"UCS2\"");
    _modem.waitResponse(500);

    for (int i = 1; i <= 30; i++) {
        String buffer = "";

        _modem.sendAT(GF("+CMGR="), i);

        unsigned long startTime = millis();
        while (millis() - startTime < 3000) {
            while (_serialAT.available()) {
                char c = _serialAT.read();
                buffer += c;
            }
            if (buffer.indexOf("OK") != -1 || buffer.indexOf("ERROR") != -1) {
                break;
            }
            delay(1);
        }

        if (buffer.indexOf("+CMGR:") == -1) {
            continue;
        }

        int headerStart = buffer.indexOf("+CMGR:");
        int headerEnd   = buffer.indexOf("\n", headerStart);
        if (headerEnd == -1) continue;

        String header = buffer.substring(headerStart, headerEnd);

        int q1 = header.indexOf('"');
        int q2 = header.indexOf('"', q1 + 1);
        if (q1 != -1 && q2 != -1 && header.substring(q1 + 1, q2) == "REC READ") continue;

        int q3 = header.indexOf('"', q2 + 1);
        int q4 = header.indexOf('"', q3 + 1);
        int q5 = header.indexOf('"', q4 + 1);
        int q6 = header.indexOf('"', q5 + 1);
        int q7 = header.indexOf('"', q6 + 1);
        int q8 = header.indexOf('"', q7 + 1);

        String number = "";
        if (q3 != -1 && q4 != -1) {
            number = header.substring(q3 + 1, q4);
        }
        String timestamp = "";
        if (q7 != -1 && q8 != -1) {
            timestamp = header.substring(q7 + 1, q8);
        }

        int textStart = headerEnd + 1;
        int textEnd   = buffer.indexOf("\nOK", textStart);
        if (textEnd == -1) textEnd = buffer.length();
        String text = buffer.substring(textStart, textEnd);
        text.trim();

        if (text.length() == 0) continue;

        _modem.sendAT("+CSCS=\"IRA\"");
        _modem.waitResponse(500);

        sms.index     = i;
        sms.textRaw   = text;
        sms.number    = decodeUCS2Hex(number);
        sms.timestamp = decodeUCS2Hex(timestamp);
        sms.text      = decodeUCS2Hex(text);

        log_i("========================================");
        log_i(">>> NEW SMS RECEIVED <<<");
        log_i("From   : %s", sms.number.c_str());
        log_i("Time   : %s", sms.timestamp.c_str());
        log_i("Message: %s", sms.text.c_str());
        log_i("========================================");

        return true;
    }

    // No unread SMS found — restore IRA charset.
    _modem.sendAT("+CSCS=\"IRA\"");
    _modem.waitResponse(500);
    return false;
}

void SMSReader::deleteMessage(int index)
{
    _modem.sendAT(GF("+CMGD="), index);
    _modem.waitResponse(2000);
    log_i("[OK] SMS at index %d deleted", index);
}
