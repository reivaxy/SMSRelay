#include "SMSSender.h"
#include "ConfigManager.h"

String SMSSender::normalizeMojibake(const String &text)
{
    String fixed = text;

    // Common UTF-8/Latin-1 mojibake sequences seen with some SMS modem paths.

    // Repair common mojibake: UTF-8 bytes decoded as Latin-1 and then re-encoded as UTF-8.
    const char badCedilla[] = { (char)0xC3, (char)0x83, (char)0xC2, (char)0xA7, 0 };
    const char badEAcute[]  = { (char)0xC3, (char)0x83, (char)0xC2, (char)0xA9, 0 };
    const char badEGrave[]  = { (char)0xC3, (char)0x83, (char)0xC2, (char)0xA8, 0 };

    const char goodCedilla[] = { (char)0xC3, (char)0xA7, 0 };
    const char goodEAcute[]  = { (char)0xC3, (char)0xA9, 0 };
    const char goodEGrave[]  = { (char)0xC3, (char)0xA8, 0 };

    fixed.replace(badCedilla, goodCedilla);
    fixed.replace(badEAcute, goodEAcute);
    fixed.replace(badEGrave, goodEGrave);

    return fixed;
}

SMSSender::SMSSender(TinyGsm &modem, Stream &serialAT, ConfigManager &configManager)
    : _modem(modem), _serialAT(serialAT), _configManager(configManager) {}

bool SMSSender::needsUCS2(const String &utf8)
{
    for (unsigned int i = 0; i < utf8.length(); i++) {
        if ((uint8_t)utf8[i] > 0x7F) return true;
    }
    return false;
}

bool SMSSender::send(const String &number, const String &text)
{
    // Check if SMS sending is disabled
    if (_configManager.getInt(ConfigManager::Param::SMS_SEND_DISABLED)) {
        log_i("[SMS] SMS sending is DISABLED - logging only: to %s", number.c_str());
        log_i("[SMS] Message (%d chars): %s", text.length(), text.c_str());
        return true;  // Return true to indicate "success" (message logged but not sent)
    }

    // Enable extended error reporting before sending any SMS
    // This ensures we get verbose error codes from all AT commands
    _modem.sendAT("+CMEE=1");
    _modem.waitResponse(500);
    
    String normalized = normalizeMojibake(text);

    if (needsUCS2(normalized)) {
        return sendLongSMS_UCS2(number, utf8ToUCS2Hex(normalized));
    }
    return sendLongSMS(number, normalized);
}

bool SMSSender::sendLongSMS(const String &number, const String &text)
{
    // Ensure IRA charset for text mode sending
    _modem.sendAT("+CSCS=\"IRA\"");
    _modem.waitResponse(500);
    delay(50);  // Ensure modem has processed charset change

    const int SMS_MAX_LEN = 160;
    if ((int)text.length() <= SMS_MAX_LEN) {
        log_i("[SMS] Sending single SMS (%d chars) to %s", text.length(), number.c_str());
        if (!_modem.sendSMS(number, text)) {
            log_e("[ERROR] Failed to send single SMS to %s", number.c_str());
            logModemError("IRA_SINGLE_SEND_FAIL");
            return false;
        }
        return true;
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
        log_i("[SMS] Sending part %d (%d chars) to %s", partNum, chunk.length(), number.c_str());
        if (!_modem.sendSMS(number, chunk)) {
            log_e("[ERROR] Failed to send part %d to %s. Modem error or timeout.", partNum, number.c_str());
            logModemError("IRA_MODE_SEND_FAIL");
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

    // Ensure UCS2 charset is set before switching to PDU mode
    log_i("[UCS2] Setting UCS2 charset and PDU mode for number %s", number.c_str());
    _modem.sendAT("+CSCS=\"UCS2\"");
    int res_charset = _modem.waitResponse(500);
    if (res_charset != 1) {
        log_w("[UCS2] +CSCS=UCS2 returned %d (expected 1)", res_charset);
        logModemError("CHARSET_SET_FAIL");
    }
    delay(50);  // Ensure modem has processed charset change

    _modem.sendAT(GF("+CMGF=0"));
    int res_cmgf = _modem.waitResponse(1000);
    if (res_cmgf != 1) {
        log_e("[UCS2] +CMGF=0 failed with response %d. Switching back to text mode.", res_cmgf);
        logModemError("PDU_MODE_SET_FAIL");
        _modem.sendAT(GF("+CMGF=1"));
        _modem.waitResponse(500);
        return false;
    }

    _modem.sendAT(GF("+CMGS="), pduLen);
    log_i("[UCS2] Sending +CMGS with pduLen=%d to %s", pduLen, number.c_str());
    int res_cmgs = _modem.waitResponse(30000L, GF(">"));
    if (res_cmgs != 1) {
        log_e("[UCS2] +CMGS failed to get > prompt. Response code: %d. Switching back to text mode.", res_cmgs);
        logModemError("CMGS_PROMPT_FAIL");
        _modem.sendAT(GF("+CMGF=1"));
        _modem.waitResponse(500);
        return false;
    }

    _serialAT.print(pdu);
    _serialAT.write(static_cast<char>(0x1A));
    _serialAT.flush();
    int res = _modem.waitResponse(60000L);
    
    if (res == 1) {
        log_i("[UCS2] PDU sent successfully to %s", number.c_str());
    } else if (res == 0) {
        log_e("[UCS2] PDU send timeout (60s) to %s. Modem may not be responding.", number.c_str());
        logModemError("PDU_SEND_TIMEOUT");
    } else {
        log_e("[UCS2] PDU send failed with error code %d to %s", res, number.c_str());
        logModemError("PDU_SEND_ERROR");
    }

    // Restore text mode and IRA charset for all other operations
    _modem.sendAT(GF("+CMGF=1"));
    _modem.waitResponse(500);
    delay(50);  // Ensure modem has processed mode change
    
    _modem.sendAT("+CSCS=\"IRA\"");
    _modem.waitResponse(500);
    delay(50);  // Ensure modem has processed charset change

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
        log_i("[SMS] Sending UCS2 part %d (%d chars) to %s", partNum, chunkHex / 4, number.c_str());
        if (!sendSingleSMS_UCS2(number, chunk)) {
            log_e("[ERROR] Failed to send UCS2 part %d to %s. Check modem connection and signal.", partNum, number.c_str());
            logModemError("UCS2_MULTIPART_SEND_FAIL");
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

void SMSSender::logModemError(const String &context)
{
    // Extended error reporting is already enabled at the start of send()
    // Query signal quality which may indicate connectivity issues
    _modem.sendAT("+CSQ");
    int csqRes = _modem.waitResponse(500);
    if (csqRes == 1) {
        log_w("[%s] Signal quality check returned response", context.c_str());
    } else {
        log_w("[%s] Unable to query signal quality (response: %d)", context.c_str(), csqRes);
    }
    
    // Try to get network registration status
    _modem.sendAT("+CREG?");
    int cregRes = _modem.waitResponse(500);
    if (cregRes == 1) {
        log_w("[%s] Network registration check returned response", context.c_str());
    } else {
        log_w("[%s] Unable to query network registration (response: %d)", context.c_str(), cregRes);
    }
}
