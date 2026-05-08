#include "SMSSender.h"

SMSSender::SMSSender(TinyGsm &modem, Stream &serialAT)
    : _modem(modem), _serialAT(serialAT) {}

bool SMSSender::needsUCS2(const String &utf8)
{
    for (unsigned int i = 0; i < utf8.length(); i++) {
        if ((uint8_t)utf8[i] > 0x7F) return true;
    }
    return false;
}

bool SMSSender::send(const String &number, const String &text)
{
    if (needsUCS2(text)) {
        return sendLongSMS_UCS2(number, utf8ToUCS2Hex(text));
    }
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

bool SMSSender::sendSingleSMS_UCS2(const String &number, const String &hexChunk)
{
    int pduLen = 0;
    String pdu = buildSMSPDU(number, hexChunk, pduLen);
    log_i("[UCS2] PDU mode, pduLen=%d", pduLen);

    _modem.sendAT(GF("+CMGF=0"));
    if (_modem.waitResponse(1000) != 1) {
        log_e("[UCS2] +CMGF=0 failed");
        return false;
    }

    _modem.sendAT(GF("+CMGS="), pduLen);
    if (_modem.waitResponse(30000L, GF(">")) != 1) {
        log_e("[UCS2] No > prompt from +CMGS");
        _modem.sendAT(GF("+CMGF=1"));
        _modem.waitResponse(500);
        return false;
    }

    _serialAT.print(pdu);
    _serialAT.write(static_cast<char>(0x1A));
    _serialAT.flush();
    int res = _modem.waitResponse(60000L);
    log_i("[UCS2] PDU send result: %d", res);

    // Restore text mode for all other operations
    _modem.sendAT(GF("+CMGF=1"));
    _modem.waitResponse(500);

    return res == 1;
}

String SMSSender::buildSMSPDU(const String &number, const String &ucs2hex, int &pduLen)
{
    String pdu = "";
    char   buf[5];

    // SMSC info: 00 = use default SMSC from SIM
    pdu += "00";

    // PDU type: SMS-SUBMIT, TP-VPF=10 (relative VP), no UDHI, no SRR
    pdu += "11";

    // Message Reference
    pdu += "00";

    // Destination Address
    String digits = number;
    if (digits.startsWith("+")) digits = digits.substring(1);

    snprintf(buf, sizeof(buf), "%02X", (int)digits.length());
    pdu += buf;                                        // DA-Length (digit count)
    pdu += number.startsWith("+") ? "91" : "81";     // DA-Type

    // BCD semi-octet encoding: swap nibbles of each digit pair, pad with F if odd
    String padded = digits;
    if (padded.length() % 2 != 0) padded += "F";
    for (unsigned int i = 0; i < padded.length(); i += 2) {
        char hi = padded[i + 1];
        char lo = padded[i];
        uint8_t byte = (uint8_t)(((hi == 'F' ? 0xF : (hi - '0')) << 4) | (lo - '0'));
        snprintf(buf, sizeof(buf), "%02X", byte);
        pdu += buf;
    }

    pdu += "00";  // PID
    pdu += "08";  // DCS: UCS-2
    pdu += "AA";  // VP: relative (~4 days)

    // UDL: number of octets in the UD (2 bytes per UCS-2 char)
    int udBytes = (int)ucs2hex.length() / 2;
    snprintf(buf, sizeof(buf), "%02X", udBytes);
    pdu += buf;

    pdu += ucs2hex;

    // AT+CMGS length = PDU byte count excluding the leading SMSC info byte
    pduLen = ((int)pdu.length() / 2) - 1;
    return pdu;
}

bool SMSSender::sendLongSMS_UCS2(const String &number, const String &hexText)
{
    const int MAX_HEX = 280; // 70 UCS-2 chars x 4 hex chars
    if ((int)hexText.length() <= MAX_HEX) {
        return sendSingleSMS_UCS2(number, hexText);
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
        if (!sendSingleSMS_UCS2(number, chunk)) {
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
