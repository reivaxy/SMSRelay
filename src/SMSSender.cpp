#include "SMSSender.h"

SMSSender::SMSSender(TinyGsm &modem) : _modem(modem) {}

bool SMSSender::send(const String &number, const String &text)
{
    return sendLongSMS(number, text);
}

bool SMSSender::sendLongSMS(const String &number, const String &text)
{
    const int SMS_MAX_LEN = 160;
    if ((int)text.length() <= SMS_MAX_LEN) {
        return _modem.sendSMS(number, text);
    }

    int offset = 0;
    int partNum = 0;
    while (offset < (int)text.length()) {
        int remaining = (int)text.length() - offset;
        int chunkLen = min(remaining, SMS_MAX_LEN);

        if (chunkLen == SMS_MAX_LEN) {
            int lastSpace = -1;
            for (int j = chunkLen - 1; j > 0; j--) {
                if (text[offset + j] == ' ') {
                    lastSpace = j;
                    break;
                }
            }
            if (lastSpace > 0) {
                chunkLen = lastSpace;
            }
        }

        String chunk = text.substring(offset, offset + chunkLen);
        chunk.trim();
        partNum++;
        log_i("[SMS] Sending part %d (%d chars)", partNum, chunk.length());
        if (!_modem.sendSMS(number, chunk)) {
            log_i("[ERROR] Failed to send part %d", partNum);
            return false;
        }

        offset += chunkLen;
        while (offset < (int)text.length() && text[offset] == ' ') offset++;
    }
    return true;
}

bool SMSSender::sendLongSMS_UCS2(const String &number, const String &hexText)
{
    const int MAX_HEX = 280; // 70 UCS-2 chars x 4 hex chars
    if ((int)hexText.length() <= MAX_HEX) {
        return _modem.sendSMS(number, hexText);
    }

    int offset = 0;
    int partNum = 0;
    while (offset < (int)hexText.length()) {
        int remaining = (int)hexText.length() - offset;
        int chunkHex = min(remaining, MAX_HEX);
        chunkHex -= (chunkHex % 4); // align to code-unit boundary

        if (chunkHex == MAX_HEX) {
            for (int j = chunkHex - 4; j >= 4; j -= 4) {
                if (hexText.substring(offset + j, offset + j + 4).equalsIgnoreCase("0020")) {
                    chunkHex = j;
                    break;
                }
            }
        }

        String chunk = hexText.substring(offset, offset + chunkHex);
        partNum++;
        log_i("[SMS] Sending UCS2 part %d (%d chars)", partNum, chunkHex / 4);
        if (!_modem.sendSMS(number, chunk)) {
            log_i("[ERROR] Failed to send UCS2 part %d", partNum);
            return false;
        }

        offset += chunkHex;
        // Skip trailing space (0020) at split point
        if (offset + 3 < (int)hexText.length() &&
            hexText.substring(offset, offset + 4).equalsIgnoreCase("0020")) {
            offset += 4;
        }
    }
    return true;
}

String SMSSender::utf8ToUCS2Hex(const String &utf8)
{
    String result = "";
    int i = 0;
    while (i < (int)utf8.length()) {
        uint16_t cp = 0;
        uint8_t b = (uint8_t)utf8[i];
        if (b < 0x80) {
            cp = b; i += 1;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < (int)utf8.length()) {
            cp = ((uint16_t)(b & 0x1F) << 6) | ((uint8_t)utf8[i + 1] & 0x3F); i += 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < (int)utf8.length()) {
            cp = ((uint16_t)(b & 0x0F) << 12) | (((uint8_t)utf8[i + 1] & 0x3F) << 6) |
                 ((uint8_t)utf8[i + 2] & 0x3F); i += 3;
        } else {
            cp = '?'; i += 1;
        }
        char buf[5];
        snprintf(buf, sizeof(buf), "%04X", cp);
        result += buf;
    }
    return result;
}
