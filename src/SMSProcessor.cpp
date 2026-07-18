#include "SMSProcessor.h"
#include "TemperatureHumidityProcessor.h"
#include "ConfigManager.h"
#include "BatteryProcessor.h"
#include "MainPowerCheck.h"
#include "AlertManager.h"
#include "ClockManager.h"
#include "OTAManager.h"
#include "utilities.h"

SMSProcessor::SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader, 
                           MainPowerCheck &mainPowerCheck, BatteryProcessor &batteryProcessor, TemperatureHumidityProcessor &tempHumidityProcessor,
                           ConfigManager &configManager, PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, ClockManager &clockManager,
                           OTAManager &otaManager)
    : _sender(sender), _targetNumber(targetNumber), _reader(reader), _mainPowerCheck(mainPowerCheck),
      _batteryProcessor(batteryProcessor), _tempHumidityProcessor(tempHumidityProcessor), 
      _configManager(configManager), _phoneNumberManager(phoneNumberManager), _alertManager(alertManager), _clockManager(clockManager),
      _otaManager(otaManager) {}

void SMSProcessor::process(const ReceivedSMS &sms)
{
    String textUpper = sms.text;
    textUpper.trim();
    textUpper.toUpperCase();

    // Log who's sending the command
    auto perm = _phoneNumberManager.getPermission(sms.number);
    String permStr = (perm == PhoneNumberManager::Permission::ADMIN) ? "ADMIN" : 
                     (perm == PhoneNumberManager::Permission::READ) ? "READ" : "NONE";
    log_i("[CMD] From: %s (Permission: %s)", sms.number.c_str(), permStr.c_str());

    // Check basic authorization
    if (perm == PhoneNumberManager::Permission::NONE) {
        log_w("[CMD] Unauthorized sender: %s", sms.number.c_str());
        _sender.send(sms.number, "ERROR: Not authorized");
        return;
    }

    // Commands that require ADMIN permission
    if (textUpper.startsWith("CONFIG ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleWriteConfigCommand(sms.text.substring(7), sms.number);
    } 
    else if (textUpper.startsWith("ADDPHONE ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleAddPhoneCommand(sms.text.substring(9), sms.number);
    }
    else if (textUpper.startsWith("REMOVEPHONE ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleRemovePhoneCommand(sms.text.substring(12), sms.number);
    }
    else if (textUpper.startsWith("MUTE ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleMuteCommand(sms.text.substring(5), sms.number);
    }
    else if (textUpper.startsWith("UNMUTE ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleUnmuteCommand(sms.text.substring(7), sms.number);
    }
    else if (textUpper.startsWith("FOR:")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleForCommand(sms.text.substring(4), sms.number);
    }
    else if (textUpper == "LIST") {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleListCommand(sms.index, sms.number);
    }
    else if (textUpper.startsWith("READ ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        int index = sms.text.substring(5).toInt();
        if (index > 0) {
            handleReadCommand(index, sms.number);
        } else {
            _sender.send(sms.number, "ERROR: read: invalid index");
        }
    }
    else if (textUpper.startsWith("DELETE ")) {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        int index = sms.text.substring(7).toInt();
        if (index > 0) {
            handleDeleteCommand(index, sms.number);
        } else {
            _sender.send(sms.number, "ERROR: delete: invalid index");
        }
    }
    // Commands that work with READ or ADMIN permission
    else if (textUpper == "STATUS") {
        handleStatusCommand(sms.number);
    } 
    else if (textUpper == "LEVELS") {
        handleLevelCommand(sms.number);
    } 
    else if (textUpper == "CONFIG") {
        handleReadConfigCommand(sms.number);
    } 
    else if (textUpper == "LISTPHONES") {
        handleListPhonesCommand(sms.number);
    }
    else if (textUpper == "LISTALERTS") {
        handleListAlertsCommand(sms.number);
    }
    else if (textUpper == "CLEAR") {
        handleClearCommand(sms.number);
    }
    else if (textUpper == "HELP") {
        handleHelpCommand(sms.number);
    }
    else if (textUpper == "OTA") {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleOTACommand(sms.number);
    }
    else if (textUpper == "RESET") {
        if (!hasPermission(sms.number, PhoneNumberManager::Permission::ADMIN)) {
            sendPermissionDenied(sms.number);
            return;
        }
        handleResetCommand(sms.number);
    }
    else if (textUpper.startsWith("ACK ")) {
        handleACKCommand(sms.text.substring(4), sms.number);
    }
}

void SMSProcessor::handleForCommand(const String &rest, const String &senderNumber)
{
    String trimmed = rest;
    trimmed.trim();

    // Extract destination number: optional leading '+', then digits and spaces
    int idx = 0;
    if (idx < (int)trimmed.length() && trimmed[idx] == '+') idx++;
    while (idx < (int)trimmed.length() &&
           (trimmed[idx] == ' ' || (trimmed[idx] >= '0' && trimmed[idx] <= '9'))) idx++;

    String destNumber = trimmed.substring(0, idx);
    destNumber.replace(" ", "");
    String outgoingText = trimmed.substring(idx);
    outgoingText.trim();

    if (destNumber.length() > 0 && outgoingText.length() > 0) {
        log_i("[CMD] Sending to: %s", destNumber.c_str());
        log_i("[CMD] Message   : %s", outgoingText.c_str());
        bool sent = _sender.send(destNumber, outgoingText);
        if (sent) {
            log_i("[OK] SMS sent successfully");
            _sender.send(_targetNumber, "OK: Message sent to " + destNumber);
        } else {
            log_i("[ERROR] Failed to send SMS");
            _sender.send(_targetNumber, "ERROR: Failed to send SMS to " + destNumber);
        }
    } else {
        log_i("[ERROR] FOR: command missing number or message body");
        _sender.send(_targetNumber, "ERROR: FOR: command missing number or message body");
    }
}


void SMSProcessor::handleReadCommand(int index, const String &senderNumber)
{
    log_i("[CMD] Read query for index %d", index);
    ReceivedSMS sms;
    if (!_reader.readAt(index, sms)) {
        _sender.send(senderNumber, "ERROR: No message at index " + String(index));
        return;
    }
    String msg = "[" + String(sms.index) + "] From: " + sms.number +
                 "\nTime: " + sms.timestamp +
                 "\n" + sms.text;
    _sender.send(senderNumber, msg);
}

void SMSProcessor::handleDeleteCommand(int index, const String &senderNumber)
{
    log_i("[CMD] Delete query for index %d", index);
    _reader.deleteMessage(index);
    _sender.send(senderNumber, "OK: Message " + String(index) + " deleted");
}

void SMSProcessor::handleStatusCommand(const String &senderNumber)
{
    log_i("[CMD] Status query received");

#ifdef BOARD_BAT_ADC_PIN
    int batAdcValue = BatteryProcessor::readBatADC();
    int batAdcThreshold = _configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD);
    bool isBattery = (batAdcValue > 0 && batAdcValue < batAdcThreshold);
    String powerSource = isBattery ? "Battery" : "USB";
    String statusMsg = "Status:\nBattery: " + String(batAdcValue) + " (" + powerSource + ")\n";
#else
    String statusMsg = "Status:\nBattery: No ADC\n";
#endif

    int mainAdcValue = MainPowerCheck::readGPIO00ADC();
    int powerAdcThreshold = _configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD);
    String mainStatus = (mainAdcValue >= powerAdcThreshold) ? "OK" : "LOW";
    statusMsg += "Main Power: " + String(mainAdcValue) + " (" + mainStatus + ")";

    log_i("[CMD] %s", statusMsg.c_str());
    _sender.send(senderNumber, statusMsg);
}

void SMSProcessor::handleListCommand(int skipIndex, const String &senderNumber)
{
    log_i("[CMD] List query received");

    const int maxEntries = 6;
    const int maxScanIndex = 30;
    int found = 0;
    String response = "Messages:";

    for (int i = 1; i <= maxScanIndex && found < maxEntries; i++) {
        if (i == skipIndex) continue;

        ReceivedSMS sms;
        if (!_reader.readAt(i, sms)) continue;

        found++;
        response += "\n[" + String(sms.index) + "] " + sms.number;
    }

    if (found == 0) {
        _sender.send(senderNumber, "No messages stored");
        return;
    }

    _sender.send(senderNumber, response);
}

void SMSProcessor::handleLevelCommand(const String &senderNumber)
{
    log_i("[CMD] Level query received");
    
    String levelMsg = "Levels:\n";
    
    // Temperature status
    float temp = _tempHumidityProcessor.getTemperature();
    float tempHigh = _configManager.getFloat(ConfigManager::Param::TEMP_HIGH);
    float tempLow = _configManager.getFloat(ConfigManager::Param::TEMP_LOW);
    
    levelMsg += "Temp: " + String(temp, 1) + "C";
    if (temp > tempHigh) {
        levelMsg += " [HIGH]";
    } else if (temp < tempLow) {
        levelMsg += " [LOW]";
    } else {
        levelMsg += " [OK]";
    }
    levelMsg += "\n";
    
    // Humidity status
    float humidity = _tempHumidityProcessor.getHumidity();
    float humidityHigh = _configManager.getFloat(ConfigManager::Param::HUMIDITY_HIGH);
    float humidityLow = _configManager.getFloat(ConfigManager::Param::HUMIDITY_LOW);
    
    levelMsg += "Humidity: " + String(humidity, 1) + "%";
    if (humidity > humidityHigh) {
        levelMsg += " [HIGH]";
    } else if (humidity < humidityLow) {
        levelMsg += " [LOW]";
    } else {
        levelMsg += " [OK]";
    }
    levelMsg += "\n";
    
    // Battery status
#ifdef BOARD_BAT_ADC_PIN
    int batAdcValue = BatteryProcessor::readBatADC();
    int batAdcThreshold = _configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD);
    int batAdcNearEmpty = _configManager.getInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY);
    
    levelMsg += "Battery: " + String(batAdcValue);
    if (batAdcValue < batAdcNearEmpty) {
        levelMsg += " [NEAR EMPTY]";
    } else if (batAdcValue < batAdcThreshold) {
        levelMsg += " [USB]";
    } else {
        levelMsg += " [OK]";
    }
    levelMsg += "\n";
#endif
    
    // Main power status
    int mainAdcValue = MainPowerCheck::readGPIO00ADC();
    int powerAdcThreshold = _configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD);
    
    levelMsg += "Main Power: " + String(mainAdcValue);
    if (mainAdcValue < powerAdcThreshold) {
        levelMsg += " [LOW]";
    } else {
        levelMsg += " [OK]";
    }
    
    log_i("[CMD] %s", levelMsg.c_str());
    _sender.send(senderNumber, levelMsg);
}

void SMSProcessor::handleReadConfigCommand(const String &senderNumber)
{
    log_i("[CMD] Config query received");
    String configMsg = "";
#ifdef GIT_REV
    configMsg += "Rev: " + String(GIT_REV) + "\n";
#endif
    configMsg += _configManager.getAllParams();
    configMsg += "Time: " + _clockManager.getFormattedDateTime() + "\n";
    log_i("[CMD] %s", configMsg.c_str());
    _sender.send(senderNumber, configMsg);
}

void SMSProcessor::handleWriteConfigCommand(const String &rest, const String &senderNumber)
{
    String trimmed = rest;
    trimmed.trim();
    
    // Parse: "CONFIG <param> <value>"
    // Format: parameter name (case insensitive) followed by value
    
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx <= 0) {
        _sender.send(senderNumber, "ERROR: CONFIG syntax: CONFIG <param> <value>");
        return;
    }
    
    String paramName = trimmed.substring(0, spaceIdx);
    String valueStr = trimmed.substring(spaceIdx + 1);
    valueStr.trim();

    if (valueStr.length() == 0) {
        _sender.send(senderNumber, "ERROR: CONFIG: missing value");
        return;
    }
    
    // Parse parameter name using centralized function
    ConfigManager::Param param;
    if (!ConfigManager::parseParamName(paramName, param)) {
        String errorMsg = "ERROR: Unknown parameter: " + paramName + "\nValid: " + ConfigManager::getValidParamNames();
        log_e("[CMD] %s", errorMsg.c_str());
        _sender.send(senderNumber, errorMsg);
        return;
    }
    
    bool success = false;
    String confirmMsg;
    
    // Determine if parameter is float, int, or string and parse accordingly
    if (param == ConfigManager::Param::TEMP_HIGH || param == ConfigManager::Param::TEMP_LOW ||
        param == ConfigManager::Param::HUMIDITY_HIGH || param == ConfigManager::Param::HUMIDITY_LOW ||
        param == ConfigManager::Param::TEMP_OFFSET || param == ConfigManager::Param::HUMIDITY_OFFSET) {
        // Float parameters
        float val = valueStr.toFloat();
        _configManager.setFloat(param, val);
        
        if (param == ConfigManager::Param::TEMP_HIGH || param == ConfigManager::Param::TEMP_LOW || param == ConfigManager::Param::TEMP_OFFSET) {
            confirmMsg = "OK: " + ConfigManager::getParamName(param) + " set to " + String(val, 1) + "°C";
        } else {
            confirmMsg = "OK: " + ConfigManager::getParamName(param) + " set to " + String(val, 1) + "%";
        }
        success = true;
    }
    else if (param == ConfigManager::Param::BAT_ADC_THRESHOLD || param == ConfigManager::Param::BAT_ADC_NEAR_EMPTY ||
             param == ConfigManager::Param::POWER_ADC_THRESHOLD || param == ConfigManager::Param::ALERT_RESEND_DELAY_MINS ||
             param == ConfigManager::Param::NTP_RESYNC_HOURS || param == ConfigManager::Param::DST_OFFSET) {
        // Int parameters
        int val = valueStr.toInt();
        _configManager.setInt(param, val);
        
        // Readback verification for debugging
        int readback = _configManager.getInt(param);
        String paramUserName = ConfigManager::getParamName(param);
        log_i("[CMD] Set %s to %d, readback: %d", paramUserName.c_str(), val, readback);
        
        if (param == ConfigManager::Param::ALERT_RESEND_DELAY_MINS) {
            if (readback == val) {
                confirmMsg = "OK: " + paramUserName + " set to " + String(val) + " minutes";
            } else {
                confirmMsg = "WARNING: " + paramUserName + " set to " + String(val) + " but readback shows " + String(readback);
                log_w("[CMD] Mismatch: wrote %d but read %d", val, readback);
            }
        }
        else if (param == ConfigManager::Param::NTP_RESYNC_HOURS) {
            if (readback == val) {
                confirmMsg = "OK: " + paramUserName + " set to " + String(val) + " hours";
            } else {
                confirmMsg = "WARNING: " + paramUserName + " set to " + String(val) + " but readback shows " + String(readback);
                log_w("[CMD] Mismatch: wrote %d but read %d", val, readback);
            }
        }
        else if (param == ConfigManager::Param::DST_OFFSET) {
            float hours = val / 3600.0f;
            if (readback == val) {
                confirmMsg = "OK: " + paramUserName + " set to " + String(hours, 1) + " hours";
            } else {
                confirmMsg = "WARNING: " + paramUserName + " set to " + String(hours, 1) + " but readback shows " + String((float)readback / 3600.0f, 1);
                log_w("[CMD] Mismatch: wrote %d but read %d", val, readback);
            }
        }
        else {
            if (readback == val) {
                confirmMsg = "OK: " + paramUserName + " set to " + String(val);
            } else {
                confirmMsg = "WARNING: " + paramUserName + " set to " + String(val) + " but readback shows " + String(readback);
                log_w("[CMD] Mismatch: wrote %d but read %d", val, readback);
            }
        }
        success = true;
    }
    else if (param == ConfigManager::Param::WIFI_SSID || param == ConfigManager::Param::WIFI_PASSWORD) {
        // String parameters (WiFi credentials)
        _configManager.setStringParam(param, valueStr);
        log_i("[CMD] Set %s (length: %d)", ConfigManager::getParamName(param).c_str(), valueStr.length());
        confirmMsg = "OK: " + ConfigManager::getParamName(param) + " set (length: " + String(valueStr.length()) + ")";
        success = true;
    }
    
    if (success) {
        log_i("[CMD] %s", confirmMsg.c_str());
        _sender.send(senderNumber, confirmMsg);
    }
}

void SMSProcessor::handleClearCommand(const String &senderNumber)
{
    log_i("[CMD] Clear command received from %s", senderNumber.c_str());
    
    // Clear all pending alerts from AlertManager
    _alertManager.clearAll();
    
    // Reset alert flags in all processors
    _tempHumidityProcessor.resetAlertFlags();
    _batteryProcessor.resetAlertFlags();
    _mainPowerCheck.resetAlertFlags();
    
    log_i("[OK] All alerts and alert flags cleared");
    _sender.send(senderNumber, "OK: All alerts cleared (ACKed and pending)");
}

bool SMSProcessor::hasPermission(const String &senderNumber, PhoneNumberManager::Permission required) {
    auto actualPerm = _phoneNumberManager.getPermission(senderNumber);
    return (int)actualPerm >= (int)required;
}

void SMSProcessor::sendPermissionDenied(const String &senderNumber) {
    log_w("[CMD] Permission denied for: %s", senderNumber.c_str());
    _sender.send(senderNumber, "ERROR: Admin permission required for this command");
}

void SMSProcessor::handleAddPhoneCommand(const String &rest, const String &senderNumber) {
    String trimmed = rest;
    trimmed.trim();
    
    // Parse: "ADDPHONE <number> admin|read [alias]"
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx <= 0) {
        _sender.send(senderNumber, "ERROR: ADDPHONE syntax: ADDPHONE <number> admin|read [alias]");
        return;
    }
    
    String number = trimmed.substring(0, spaceIdx);
    number.trim();
    
    String rest2 = trimmed.substring(spaceIdx + 1);
    rest2.trim();
    
    int spaceIdx2 = rest2.indexOf(' ');
    String permStr, alias;
    
    if (spaceIdx2 <= 0) {
        // No alias provided
        permStr = rest2;
        alias = "";
    } else {
        // Alias provided
        permStr = rest2.substring(0, spaceIdx2);
        alias = rest2.substring(spaceIdx2 + 1);
        alias.trim();
    }
    
    permStr.toUpperCase();
    
    PhoneNumberManager::Permission perm;
    if (permStr == "ADMIN") {
        perm = PhoneNumberManager::Permission::ADMIN;
    } else if (permStr == "READ") {
        perm = PhoneNumberManager::Permission::READ;
    } else {
        _sender.send(senderNumber, "ERROR: Permission must be 'admin' or 'read'");
        return;
    }
    
    if (_phoneNumberManager.addPhoneNumber(number, perm, alias)) {
        log_i("[CMD] Phone added: %s with %s permission", number.c_str(), permStr.c_str());
        String confirmMsg = "OK: Phone " + number + " added with " + permStr + " permission";
        if (alias.length() > 0) {
            confirmMsg += " (alias: " + alias + ")";
        }
        _sender.send(senderNumber, confirmMsg);
        _sender.send(number, "Phone number added to SMS Relay with " + permStr + " permission\nSend HELP for the list of commands");
    } else {
        _sender.send(senderNumber, "ERROR: Failed to add phone (max 5 additional numbers allowed)");
    }
}

void SMSProcessor::handleRemovePhoneCommand(const String &rest, const String &senderNumber) {
    String param = rest;
    param.trim();
    
    if (param.length() == 0) {
        _sender.send(senderNumber, "ERROR: REMOVEPHONE syntax: REMOVEPHONE <index|number>");
        return;
    }
    
    if (_phoneNumberManager.removePhoneNumber(param)) {
        log_i("[CMD] Phone removed: %s", param.c_str());
        _sender.send(senderNumber, "OK: Phone " + param + " removed");
    } else {
        _sender.send(senderNumber, "ERROR: Phone not found, is root, or invalid index. Run LISTPHONES to see available entries");
    }
}

void SMSProcessor::handleListPhonesCommand(const String &senderNumber) {
    log_i("[CMD] List phones requested");
    String response = _phoneNumberManager.getFormattedList();
    _sender.send(senderNumber, response);
}

void SMSProcessor::handleListAlertsCommand(const String &senderNumber) {
    log_i("[CMD] List alerts requested");
    String response = _alertManager.getPendingAlertsList();
    _sender.send(senderNumber, response);
}

void SMSProcessor::handleHelpCommand(const String &senderNumber) {
    log_i("[CMD] Help command received");
    
    auto perm = _phoneNumberManager.getPermission(senderNumber);
    String helpMsg = "Available Commands:\n\n";
    
    // Commands accessible to both READ and ADMIN
    helpMsg += "STATUS - Device status\n";
    helpMsg += "LEVELS - Sensor levels\n";
    helpMsg += "CONFIG - View params\n";
    helpMsg += "LISTPHONES - List phones\n";
    helpMsg += "LISTALERTS - List pending alerts\n";
    helpMsg += "CLEAR - Reset alerts\n";
    helpMsg += "ACK <code> - Acknowledge alert\n";
    
    // Admin-only commands
    if (perm == PhoneNumberManager::Permission::ADMIN) {
        helpMsg += "\nADMIN:\n";
        helpMsg += "LIST - List messages\n";
        helpMsg += "READ <i> - Read msg i\n";
        helpMsg += "DELETE <i> - Delete msg i\n";
        helpMsg += "FOR:<num> <msg> - Forward\n";
        helpMsg += "CONFIG <p> <v> - Set param\n";
        helpMsg += "ADDPHONE <n><p>[a] - Add\n";
        helpMsg += "REMOVEPHONE <i|n> - Remove\n";
        helpMsg += "MUTE <i|n|me> - Mute alerts\n";
        helpMsg += "UNMUTE <i|n|me> - Unmute alerts\n";
        helpMsg += "OTA - Firmware update\n";
        helpMsg += "RESET - Restart device\n";
        helpMsg += "\nLegend:\n";
        helpMsg += "<i|n>=index/number\n";
        helpMsg += "<p>=admin|read\n";
        helpMsg += "[a]=opt alias";
        
        helpMsg += "\n\nParams: " + ConfigManager::getValidParamNames();
    }
    
    log_i("[CMD] Help displayed");
    _sender.send(senderNumber, helpMsg);
}

void SMSProcessor::handleMuteCommand(const String &rest, const String &senderNumber) {
    String param = rest;
    param.trim();
    param.toUpperCase();
    
    if (param.length() == 0) {
        _sender.send(senderNumber, "ERROR: MUTE syntax: MUTE <index|number|me>");
        return;
    }
    
    // Replace "me" with the sender's number
    if (param == "ME") {
        param = senderNumber;
        log_i("[CMD] MUTE command using 'me', resolved to: %s", param.c_str());
    }
    
    if (_phoneNumberManager.muteNumber(param)) {
        log_i("[CMD] Phone muted: %s", param.c_str());
        String confirmMsg = (param == senderNumber) ? "OK: Your number muted (you won't receive alerts)" : "OK: Phone " + param + " muted (won't receive alerts)";
        _sender.send(senderNumber, confirmMsg);
    } else {
        _sender.send(senderNumber, "ERROR: Phone not found or invalid index. Run LISTPHONES to see available entries");
    }
}

void SMSProcessor::handleUnmuteCommand(const String &rest, const String &senderNumber) {
    String param = rest;
    param.trim();
    param.toUpperCase();
    
    if (param.length() == 0) {
        _sender.send(senderNumber, "ERROR: UNMUTE syntax: UNMUTE <index|number|me>");
        return;
    }
    
    // Replace "me" with the sender's number
    if (param == "ME") {
        param = senderNumber;
        log_i("[CMD] UNMUTE command using 'me', resolved to: %s", param.c_str());
    }
    
    if (_phoneNumberManager.unmuteNumber(param)) {
        log_i("[CMD] Phone unmuted: %s", param.c_str());
        String confirmMsg = (param == senderNumber) ? "OK: Your number unmuted (you will receive alerts)" : "OK: Phone " + param + " unmuted (will receive alerts)";
        _sender.send(senderNumber, confirmMsg);
    } else {
        _sender.send(senderNumber, "ERROR: Phone not found or invalid index. Run LISTPHONES to see available entries");
    }
}

void SMSProcessor::handleACKCommand(const String &rest, const String &senderNumber) {
    String code = rest;
    code.trim();
    code.toUpperCase();
    
    log_i("[CMD] ACK command received from %s with code: '%s' (length: %d)", senderNumber.c_str(), code.c_str(), code.length());
    
    // Validate code is exactly 3 digits
    if (code.length() != 3) {
        log_w("[CMD] ACK validation failed: code length is %d, expected 3", code.length());
        _sender.send(senderNumber, "ERROR: ACK code must be 3 digits (e.g., ACK 123)");
        return;
    }
    
    // Check each digit
    if (code[0] < '0' || code[0] > '9' || 
        code[1] < '0' || code[1] > '9' || 
        code[2] < '0' || code[2] > '9') {
        log_w("[CMD] ACK validation failed: code '%s' contains non-digit characters", code.c_str());
        _sender.send(senderNumber, "ERROR: ACK code must be 3 digits (e.g., ACK 123)");
        return;
    }
    
    log_i("[CMD] ACK code validated. Processing acknowledgment for code %s from %s", code.c_str(), senderNumber.c_str());
    
    if (_alertManager.handleACK(senderNumber, code)) {
        log_i("[OK] Alert [%s] acknowledged by %s", code.c_str(), senderNumber.c_str());
        _sender.send(senderNumber, "OK: Alert [" + code + "] acknowledged");
    } else {
        log_w("[CMD] ACK failed: unknown alert code [%s] from %s", code.c_str(), senderNumber.c_str());
        _sender.send(senderNumber, "ERROR: Unknown alert code [" + code + "]");
    }
}

void SMSProcessor::handleOTACommand(const String &senderNumber) {
    log_i("[CMD] OTA command received from %s", senderNumber.c_str());
    
    // Check if alert list is empty
    String alertsList = _alertManager.getPendingAlertsList();
    
    // Simple check: if the message doesn't contain any active alerts, proceed
    // The alertsList will contain just the header if empty
    if (alertsList.indexOf("[") != -1) {
        // Found an alert (format is [code], so if no '[' it's just the header)
        log_w("[CMD] OTA blocked: pending alerts exist");
        _sender.send(senderNumber, "ERROR: OTA blocked due to pending alerts.\nClear alerts with: CLEAR");
        return;
    }
    
    log_i("[CMD] No pending alerts, starting OTA mode");
    
    // Set the requester's number for sending the URL
    _otaManager.setRequesterNumber(senderNumber);
    
    // Start OTA mode
    if (_otaManager.init()) {
        log_i("[OK] OTA mode started successfully");
        // URL is sent inside init() via sendURLtoSMS()
    } else {
        log_e("[CMD] Failed to start OTA mode");
        _sender.send(senderNumber, "ERROR: Failed to start OTA mode. Check WiFi config.");
    }
}

void SMSProcessor::handleResetCommand(const String &senderNumber) {
    log_i("[CMD] RESET command received from %s", senderNumber.c_str());
    
    // Send confirmation message
    _sender.send(senderNumber, "OK: Device restarting...");
    
    // Give the modem time to send the message before restarting
    delay(1000);
    
    log_i("[CMD] Executing device restart");
    ESP.restart();
}


