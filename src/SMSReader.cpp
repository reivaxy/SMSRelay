#include "SMSReader.h"
#include "SMSProcessor.h"
#include "SMSForwarder.h"

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
        
        // Detect and fix little-endian UCS-2: if cp looks wrong but byte-swapped looks correct
        // Common check: if high byte is 0x00 and low byte is ASCII, it's likely big-endian (correct)
        // If high byte is non-zero and low byte is 0x00, it might be little-endian
        if ((cp & 0xFF) == 0x00 && (cp >> 8) != 0x00) {
            // Likely little-endian: swap bytes
            uint16_t swapped = ((cp & 0xFF) << 8) | ((cp >> 8) & 0xFF);
            // Only swap if the swapped version looks more reasonable (ASCII or common Unicode range)
            if (swapped < 0x0800) {
                cp = swapped;
            }
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
    // Read SMS in default IRA mode (don't switch to UCS2) to avoid modem firmware bug
    // where underscore (0x5F in GSM) gets incorrectly converted to 0x0011 in UCS-2.
    // IRA mode preserves underscore as ASCII 0x5F.

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

        // If modem not responding to AT commands, stop scanning and return early
        if (buffer.indexOf("ERROR") != -1 || (buffer.length() == 0 && millis() - startTime >= 3000)) {
            log_w("[WARN] Modem not responding, stopping SMS scan");
            return false;
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

        sms.index     = i;
        sms.textRaw   = text;
        sms.number    = decodeUCS2Hex(number);
        sms.timestamp = decodeUCS2Hex(timestamp);
        sms.text      = isHexUCS2(text) ? decodeUCS2Hex(text) : text;  // Decode if UCS-2 hex (diacritics), else use as-is (plain ASCII)

        log_i("========================================");
        log_i(">>> NEW SMS RECEIVED <<<");
        log_i("From   : %s", sms.number.c_str());
        log_i("Time   : %s", sms.timestamp.c_str());
        log_i("Raw hex: %s", sms.textRaw.c_str());
        log_i("Message: %s", sms.text.c_str());
        log_i("========================================");

        return true;
    }

    // No unread SMS found - no charset restoration needed (stayed in IRA)
    return false;
}

bool SMSReader::readAt(int index, ReceivedSMS &sms)
{
    // Read SMS in default IRA mode (don't switch to UCS2) to avoid modem firmware bug.
    // IRA mode preserves underscore as ASCII 0x5F.

    String buffer = "";
    _modem.sendAT(GF("+CMGR="), index);

    unsigned long startTime = millis();
    while (millis() - startTime < 3000) {
        while (_serialAT.available()) {
            char c = _serialAT.read();
            buffer += c;
        }
        if (buffer.indexOf("OK") != -1 || buffer.indexOf("ERROR") != -1) break;
        delay(1);
    }

    if (buffer.indexOf("+CMGR:") == -1) {
        return false;
    }

    int headerStart = buffer.indexOf("+CMGR:");
    int headerEnd   = buffer.indexOf("\n", headerStart);
    if (headerEnd == -1) return false;

    String header = buffer.substring(headerStart, headerEnd);

    int q1 = header.indexOf('"');
    int q2 = header.indexOf('"', q1 + 1);
    int q3 = header.indexOf('"', q2 + 1);
    int q4 = header.indexOf('"', q3 + 1);
    int q5 = header.indexOf('"', q4 + 1);
    int q6 = header.indexOf('"', q5 + 1);
    int q7 = header.indexOf('"', q6 + 1);
    int q8 = header.indexOf('"', q7 + 1);

    String number = (q3 != -1 && q4 != -1) ? header.substring(q3 + 1, q4) : "";
    String timestamp = (q7 != -1 && q8 != -1) ? header.substring(q7 + 1, q8) : "";

    int textStart = headerEnd + 1;
    int textEnd   = buffer.indexOf("\nOK", textStart);
    if (textEnd == -1) textEnd = buffer.length();
    String text = buffer.substring(textStart, textEnd);
    text.trim();

    if (text.length() == 0) {
        log_i("[WARN] SMS at index %d has empty body", index);
        return false;
    }

    sms.index     = index;
    sms.textRaw   = text;
    sms.number    = decodeUCS2Hex(number);
    sms.timestamp = decodeUCS2Hex(timestamp);
    sms.text      = isHexUCS2(text) ? decodeUCS2Hex(text) : text;  // Decode if UCS-2 hex (diacritics), else use as-is (plain ASCII)
    return true;
}

void SMSReader::deleteMessage(int index)
{
    _modem.sendAT(GF("+CMGD="), index);
    _modem.waitResponse(2000);
    log_i("[OK] SMS at index %d deleted", index);
}

void SMSReader::check(const String &targetNumber, SMSProcessor &processor, SMSForwarder &forwarder, PhoneNumberManager &phoneNumberManager)
{
    ReceivedSMS sms;
    if (!readNext(sms)) return;

    bool forwarded;
    // Check if sender is authorized (root or authorized phone with READ/ADMIN permission)
    auto permission = phoneNumberManager.getPermission(sms.number);
    if (permission != PhoneNumberManager::Permission::NONE) {
        // Authorized user - send to processor for command handling
        processor.process(sms);
        forwarded = true;
    } else {
        // Unauthorized - forward to target number
        forwarded = forwarder.forward(sms);
    }

    if (forwarded) {
        deleteMessage(sms.index);
    } else {
        log_i("[INFO] SMS at index %d kept as read (forward failed)", sms.index);
    }
}
