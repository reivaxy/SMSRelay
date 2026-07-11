#include "SerialConsole.h"
#include "ConfigManager.h"

// Returns true if the address looks like a dialable phone number
// (digits, +, *, # only). Returns false for alphanumeric sender IDs.
static bool isPhoneNumber(const String &s)
{
    if (s.length() == 0) return false;
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#'))
            return false;
    }
    return true;
}

// Returns a display-ready sender string: phone numbers as-is, alphanumeric in quotes.
static String formatSender(const String &number)
{
    if (isPhoneNumber(number)) return number;
    return "\"" + number + "\"";
}

SerialConsole::SerialConsole(SMSReader &reader, SMSForwarder &forwarder,
                             BatteryProcessor &batteryProcessor, MainPowerCheck &mainPowerCheck,
                             ConfigManager &configManager)
    : _reader(reader), _forwarder(forwarder), 
      _batteryProcessor(batteryProcessor), _mainPowerCheck(mainPowerCheck), _configManager(configManager) {}

void SerialConsole::check()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            _inputBuffer.trim();
            if (_inputBuffer.length() > 0) {
                processLine(_inputBuffer);
            }
            _inputBuffer = "";
        } else {
            _inputBuffer += c;
        }
    }
}

void SerialConsole::processLine(const String &line)
{
    String upper = line;
    upper.toUpperCase();

    if (upper == "LIST") {
        handleList();
    } else if (upper.startsWith("READ ")) {
        int index = line.substring(5).toInt();
        if (index > 0) {
            handleRead(index);
        } else {
            Serial.println("[ERROR] read: invalid index");
        }
    } else if (upper.startsWith("DELETE ")) {
        int index = line.substring(7).toInt();
        if (index > 0) {
            handleDelete(index);
        } else {
            Serial.println("[ERROR] delete: invalid index");
        }
    } else if (upper.startsWith("FORWARD ")) {
        int index = line.substring(8).toInt();
        if (index > 0) {
            handleForward(index);
        } else {
            Serial.println("[ERROR] forward: invalid index");
        }
    } else if (upper == "STATUS") {
        handleStatus();
    } else {
        Serial.println("[CONSOLE] Unknown command. Use: list | read X | delete X | forward X | status");
    }
}

void SerialConsole::handleList()
{
    Serial.println("[CONSOLE] Listing all stored messages...");
    int found = 0;
    for (int i = 1; i <= 30; i++) {
        ReceivedSMS sms;
        if (!_reader.readAt(i, sms)) continue;
        found++;
        // Build a short preview: first ~6 words or 40 chars, whichever is shorter
        String preview = sms.text;
        preview.trim();
        int wordCount = 0;
        int cutAt = preview.length();
        for (int j = 0; j < (int)preview.length(); j++) {
            if (preview[j] == ' ') {
                wordCount++;
                if (wordCount >= 6) { cutAt = j; break; }
            }
        }
        if (cutAt > 40) cutAt = 40;
        String previewStr = preview.substring(0, cutAt);
        if (cutAt < (int)preview.length()) previewStr += "...";
        Serial.printf("  [%2d] %-20s %s\n", sms.index, formatSender(sms.number).c_str(), previewStr.c_str());
    }
    if (found == 0) {
        Serial.println("  (no messages stored)");
    } else {
        Serial.printf("  Total: %d message(s)\n", found);
    }
}

void SerialConsole::handleRead(int index)
{
    Serial.printf("[CONSOLE] Reading SMS at index %d...\n", index);
    ReceivedSMS sms;
    if (_reader.readAt(index, sms)) {
        Serial.printf("  Index    : %d\n", sms.index);
        const char *fromLabel = isPhoneNumber(sms.number) ? "From     :" : "From(name):";
        Serial.printf("  %s %s\n", fromLabel, sms.number.c_str());
        Serial.printf("  Timestamp: %s\n", sms.timestamp.c_str());
        Serial.printf("  Message  : %s\n", sms.text.c_str());
    } else {
        Serial.printf("[ERROR] No SMS found at index %d\n", index);
    }
}

void SerialConsole::handleDelete(int index)
{
    Serial.printf("[CONSOLE] Deleting SMS at index %d...\n", index);
    _reader.deleteMessage(index);
    Serial.printf("[OK] SMS at index %d deleted\n", index);
}

void SerialConsole::handleForward(int index)
{
    Serial.printf("[CONSOLE] Forwarding SMS at index %d...\n", index);
    ReceivedSMS sms;
    if (!_reader.readAt(index, sms)) {
        Serial.printf("[ERROR] No SMS found at index %d\n", index);
        return;
    }
    if (_forwarder.forward(sms)) {
        Serial.printf("[OK] SMS at index %d forwarded\n", index);
    } else {
        Serial.printf("[ERROR] Failed to forward SMS at index %d\n", index);
    }
}

void SerialConsole::handleStatus()
{
    Serial.println("[CONSOLE] System Status:");
    
    int batteryADC = BatteryProcessor::readBatADC();
    int mainPowerADC = MainPowerCheck::readGPIO00ADC();
    
    int batAdcThreshold = _configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD);
    int batAdcNearEmpty = _configManager.getInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY);
    int powerAdcThreshold = _configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD);
    
    Serial.printf("  Battery Level (ADC)     : %d", batteryADC);
    if (batteryADC > batAdcThreshold) {
        Serial.println(" [USB Power]");
    } else if (batteryADC < batAdcNearEmpty) {
        Serial.println(" [Battery Near Empty]");
    } else {
        Serial.println(" [Battery Power]");
    }
    
    Serial.printf("  Main Power Level (ADC)  : %d", mainPowerADC);
    if (mainPowerADC >= powerAdcThreshold) {
        Serial.println(" [OK]");
    } else {
        Serial.println(" [LOW]");
    }
}

