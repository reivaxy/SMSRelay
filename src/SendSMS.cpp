/**
 * @file      SendSMS.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2023-11-16
 * @note      SIM7670G - SIM7670G-MNGV 2374B04 version supports SMS function,
 *            but it requires the operator base station to support SMS Over SGS service to send, otherwise it will be invalid
 *            `A7670E-LNXY-UBL` this version does not support voice and SMS functions.
 */

 #include <Arduino.h>
#include "utilities.h"
#include "secret.h"

#ifdef LILYGO_SIM7000G
#warning "SIM7000G SMS function, the network access mode must be GSM, not NB-IOT"
#endif

#ifdef TINY_GSM_MODEM_SIM7080
#error "This modem not sms function"
#endif

// Define the serial console for debug prints, if needed
#define TINY_GSM_DEBUG SerialMon

// Set serial for debug console (to the Serial Monitor, default speed 115200)
#define SerialMon Serial

// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS


// SMS message structure
struct ReceivedSMS {
    String number;
    String text;
    int index;
};

#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS  // if enabled it requires the streamDebugger lib
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif


// It depends on the operator whether to set up an APN. If some operators do not set up an APN,
// they will be rejected when registering for the network. You need to ask the local operator for the specific APN.
// APNs from other operators are welcome to submit PRs for filling.
//#define NETWORK_APN     "ctlte"             //ctlte: China Telecom

void setup()
{
    Serial.begin(115200);
#ifdef BOARD_POWERON_PIN
    /* Set Power control pin output
    * * @note      Known issues, ESP32 (V1.2) version of T-A7670, T-A7608,
    *            when using battery power supply mode, BOARD_POWERON_PIN (IO12) must be set to high level after esp32 starts, otherwise a reset will occur.
    * */
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

    // Set modem reset pin ,reset modem
#ifdef MODEM_RESET_PIN
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL); delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL); delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

#ifdef MODEM_FLIGHT_PIN
    // If there is an airplane mode control, you need to exit airplane mode
    pinMode(MODEM_FLIGHT_PIN, OUTPUT);
    digitalWrite(MODEM_FLIGHT_PIN, HIGH);
#endif

#ifdef MODEM_DTR_PIN
    // Pull down DTR to ensure the modem is not in sleep state
    log_i("Set DTR pin %d LOW", MODEM_DTR_PIN);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
#endif

    // Turn on modem
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

#ifdef MODEM_RING_PIN
    // Set ring pin input
    pinMode(MODEM_RING_PIN, INPUT_PULLUP);
#endif

    // Set modem baud
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    log_i("Start modem...");
    delay(3000);

    while (!modem.testAT()) {
        delay(10);
    }


    // Wait PB DONE
    log_i("Wait SMS Done.");
    if (!modem.waitResponse(100000UL, "SMS DONE")) {
        log_i("Can't wait from sms register ....");
        return;
    }


#ifdef NETWORK_APN
    log_i("Set network apn : %s", NETWORK_APN);
    if (!modem.setNetworkAPN(NETWORK_APN)) {
        log_i("Set network apn error !");
    }
#endif


    // Check network registration status and network signal status
    int16_t sq ;
    log_i("Wait for the modem to register with the network.");
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
        status = modem.getRegistrationStatus();
        switch (status) {
        case REG_UNREGISTERED:
        case REG_SEARCHING:
            sq = modem.getSignalQuality();
            log_i("[%lu] Signal Quality:%d", millis() / 1000, sq);
            delay(1000);
            break;
        case REG_DENIED:
            log_i("Network registration was rejected, please check if the APN is correct");
            return ;
        case REG_OK_HOME:
            log_i("Online registration successful");
            break;
        case REG_OK_ROAMING:
            log_i("Network registration successful, currently in roaming mode");
            break;
        default:
            log_i("Registration Status:%d", status);
            delay(1000);
            break;
        }
    }
    log_i("Registration Status:%d", status);
    delay(1000);

    log_i("Init success, ready to receive and forward SMS to %s", SMS_TARGET);
    
    // Enable SMS text mode
    modem.sendAT("+CMGF=1");
    modem.waitResponse();
    
    // Set up to show SMS indication
    modem.sendAT("+CNMI=2,1,0,1");
    modem.waitResponse();

    // Default charset: IRA (ASCII). handleSMS switches to UCS2 for reading.
    modem.sendAT("+CSCS=\"IRA\"");
    modem.waitResponse();
}

// Send a potentially long message as one or more 160-character SMS parts.
// Returns true only if every part was sent successfully.
bool sendLongSMS(const String &number, const String &text)
{
    const int SMS_MAX_LEN = 160;
    if ((int)text.length() <= SMS_MAX_LEN) {
        return modem.sendSMS(number, text);
    }

    int offset = 0;
    int partNum = 0;
    while (offset < (int)text.length()) {
        int remaining = (int)text.length() - offset;
        int chunkLen = min(remaining, SMS_MAX_LEN);

        // Not the last chunk — try to break at the last space within the limit
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
        if (!modem.sendSMS(number, chunk)) {
            log_i("[ERROR] Failed to send part %d", partNum);
            return false;
        }

        // Advance past the chunk and any whitespace at the split point
        offset += chunkLen;
        while (offset < (int)text.length() && text[offset] == ' ') offset++;
    }
    return true;
}

// Returns true if s is a non-empty hex UCS-2 string: only [0-9A-Fa-f], length divisible by 4.
static bool isHexUCS2(const String &s) {
    if (s.length() == 0 || s.length() % 4 != 0) return false;
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

// Decode a hex-encoded UCS-2 string to UTF-8.
// Returns the original string unchanged if it does not look like hex UCS-2.
static String decodeUCS2Hex(const String &s) {
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

// Encode a UTF-8 string to hex UCS-2 (4 uppercase hex chars per code unit).
static String utf8ToUCS2Hex(const String &utf8) {
    String result = "";
    int i = 0;
    while (i < (int)utf8.length()) {
        uint16_t cp = 0;
        uint8_t b = (uint8_t)utf8[i];
        if (b < 0x80) {
            cp = b; i += 1;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < (int)utf8.length()) {
            cp = ((uint16_t)(b & 0x1F) << 6) | ((uint8_t)utf8[i+1] & 0x3F); i += 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < (int)utf8.length()) {
            cp = ((uint16_t)(b & 0x0F) << 12) | (((uint8_t)utf8[i+1] & 0x3F) << 6) | ((uint8_t)utf8[i+2] & 0x3F); i += 3;
        } else {
            cp = '?'; i += 1;
        }
        char buf[5];
        snprintf(buf, sizeof(buf), "%04X", cp);
        result += buf;
    }
    return result;
}

// Send a hex-encoded UCS-2 message in one or more <=70-char (280 hex) parts.
// Splits preferably at a UCS-2 space (0020) boundary.
// Caller must set +CSCS="UCS2" before calling and restore it afterwards.
static bool sendLongSMS_UCS2(const String &number, const String &hexText) {
    const int MAX_HEX = 280; // 70 UCS-2 chars x 4 hex chars
    if ((int)hexText.length() <= MAX_HEX) {
        return modem.sendSMS(number, hexText);
    }
    int offset = 0, partNum = 0;
    while (offset < (int)hexText.length()) {
        int remaining = (int)hexText.length() - offset;
        int chunkHex = min(remaining, MAX_HEX);
        chunkHex -= (chunkHex % 4); // align to code-unit boundary
        if (chunkHex == MAX_HEX) {
            for (int j = chunkHex - 4; j >= 4; j -= 4) {
                if (hexText.substring(offset + j, offset + j + 4).equalsIgnoreCase("0020")) {
                    chunkHex = j; break;
                }
            }
        }
        String chunk = hexText.substring(offset, offset + chunkHex);
        partNum++;
        log_i("[SMS] Sending UCS2 part %d (%d chars)", partNum, chunkHex / 4);
        if (!modem.sendSMS(number, chunk)) {
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

void handleSMS() {
    // Switch to UCS2: modem returns message body as unambiguous hex, avoiding
    // GSM7<->charset mapping issues that corrupt diacritics.
    modem.sendAT("+CSCS=\"UCS2\"");
    modem.waitResponse(500);

    // Try to read SMS from each index
    for (int i = 1; i <= 30; i++) {
        String smsNumber = "";
        String smsText = "";
        String buffer = "";

        // Send the read command — do NOT call waitResponse yet, we need to capture the raw output
        modem.sendAT(GF("+CMGR="), i);

        // Read the full modem response until "OK" or "ERROR" is received (or timeout)
        unsigned long startTime = millis();
        while (millis() - startTime < 3000) {
            while (SerialAT.available()) {
                char c = SerialAT.read();
                buffer += c;
            }
            if (buffer.indexOf("OK") != -1 || buffer.indexOf("ERROR") != -1) {
                break;
            }
            delay(1);
        }

        // No SMS at this index
        if (buffer.indexOf("+CMGR:") == -1) {
            continue;
        }

        // Parse response format:
        // +CMGR: "REC UNREAD","<number>","","<timestamp>"\r\n<message text>\r\nOK
        int headerStart = buffer.indexOf("+CMGR:");
        int headerEnd   = buffer.indexOf("\n", headerStart);

        if (headerEnd != -1) {
            String header = buffer.substring(headerStart, headerEnd);

            // Header format: +CMGR: "REC UNREAD","<number>","","<timestamp>"
            // Fields (0-based quote pairs): 0=status, 1=number, 2=name(empty), 3=timestamp
            int q1 = header.indexOf('"');
            int q2 = header.indexOf('"', q1 + 1);
            if (q1 != -1 && q2 != -1 && header.substring(q1 + 1, q2) == "REC READ") continue;
            int q3 = header.indexOf('"', q2 + 1);
            int q4 = header.indexOf('"', q3 + 1);
            int q5 = header.indexOf('"', q4 + 1);
            int q6 = header.indexOf('"', q5 + 1);
            int q7 = header.indexOf('"', q6 + 1);
            int q8 = header.indexOf('"', q7 + 1);
            if (q3 != -1 && q4 != -1) {
                smsNumber = header.substring(q3 + 1, q4);
            }
            String smsTimestamp = "";
            if (q7 != -1 && q8 != -1) {
                smsTimestamp = header.substring(q7 + 1, q8);
            }

            // Extract message text (line after header, before OK)
            int textStart = headerEnd + 1;
            int textEnd   = buffer.indexOf("\nOK", textStart);
            if (textEnd == -1) textEnd = buffer.length();
            smsText = buffer.substring(textStart, textEnd);
            smsText.trim();

            if (smsText.length() == 0) {
                continue;
            }

            // Save raw hex UCS-2 body for forwarding (preserves encoding perfectly).
            // Decode fields to UTF-8 for logging and string operations.
            String smsTextRaw = smsText;
            smsNumber    = decodeUCS2Hex(smsNumber);
            smsTimestamp = decodeUCS2Hex(smsTimestamp);
            smsText      = decodeUCS2Hex(smsText);

            // Restore IRA for all outgoing plain-ASCII messages.
            modem.sendAT("+CSCS=\"IRA\"");
            modem.waitResponse(500);

            log_i("========================================");
            log_i(">>> NEW SMS RECEIVED <<<");
            log_i("From   : %s", smsNumber.c_str());
            log_i("Time   : %s", smsTimestamp.c_str());
            log_i("Message: %s", smsText.c_str());
            log_i("========================================");

            // Check if this is a send command from SMS_TARGET:
            // Format: "FOR:<number> <message body>"
            String smsTextUpper = smsText;
            smsTextUpper.toUpperCase();
            bool forwarded = true;
            if (smsNumber == SMS_TARGET && smsTextUpper.startsWith("FOR:")) {
                // Extract everything after "FOR:"
                String rest = smsText.substring(4);
                rest.trim();

                // Extract destination number: optional leading '+', then digits and spaces
                // First character that is not a digit or space ends the number
                int idx = 0;
                if (idx < rest.length() && rest[idx] == '+') idx++;
                while (idx < rest.length() && (rest[idx] == ' ' || (rest[idx] >= '0' && rest[idx] <= '9'))) idx++;

                String destNumber = rest.substring(0, idx);
                // Remove spaces from the number
                destNumber.replace(" ", "");
                String outgoingText = rest.substring(idx);
                outgoingText.trim();

                if (destNumber.length() > 0 && outgoingText.length() > 0) {
                    log_i("[CMD] Sending to: %s", destNumber.c_str());
                    log_i("[CMD] Message   : %s", outgoingText.c_str());

                    bool sent = sendLongSMS(destNumber, outgoingText);
                    if (sent) {
                        log_i("[OK] SMS sent successfully");
                        modem.sendSMS(SMS_TARGET, "OK: Message sent to " + destNumber);
                    } else {
                        log_i("[ERROR] Failed to send SMS");
                        modem.sendSMS(SMS_TARGET, "ERROR: Failed to send SMS to " + destNumber);
                    }
                } else {
                    log_i("[ERROR] FOR: command missing number or message body");
                    modem.sendSMS(SMS_TARGET, "ERROR: FOR: command missing number or message body");
                }
            } else {
                // Normal incoming SMS — send header and body in IRA/text mode
                String headerMessage = "Fwd from: " + smsNumber + "\nTime: " + smsTimestamp;
                bool sent1 = modem.sendSMS(SMS_TARGET, headerMessage);
                log_i("%s", sent1 ? "[OK] Header SMS forwarded" : "[ERROR] Failed to forward header SMS");

                bool sent2 = sendLongSMS(SMS_TARGET, smsText);
                forwarded = sent2;
                if (sent2) {
                    log_i("[OK] Body SMS forwarded");
                } else {
                    log_i("[ERROR] Failed to forward body SMS");
                    modem.sendSMS(SMS_TARGET, "ERROR: Failed to forward SMS from " + smsNumber);
                }
            }

            if (forwarded) {
                modem.sendAT(GF("+CMGD="), i);
                modem.waitResponse(2000);
                log_i("[OK] SMS at index %d deleted", i);
            } else {
                log_i("[INFO] SMS at index %d kept as read (forward failed)", i);
            }

            return; // Process one SMS per call
        }
    }
    // No SMS found — restore IRA charset.
    modem.sendAT("+CSCS=\"IRA\"");
    modem.waitResponse(500);
}

void loop()
{
    // Check for incoming SMS periodically
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 2000) {
        lastCheck = millis();
        handleSMS();
    }

    if (SerialAT.available()) {
        Serial.write(SerialAT.read());
    }
    if (Serial.available()) {
        SerialAT.write(Serial.read());
    }
    delay(100);
}

#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGO-T-A76XX/tree/main/lib) to the arduino libraries directory , See README"
#endif
