#include "SMSProcessor.h"
#include "TemperatureHumidityProcessor.h"
#include "ConfigManager.h"
#include "BatteryProcessor.h"
#include "MainPowerCheck.h"
#include "utilities.h"

SMSProcessor::SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader, 
                           MainPowerCheck &mainPowerCheck, BatteryProcessor &batteryProcessor, TemperatureHumidityProcessor &tempHumidityProcessor,
                           ConfigManager &configManager, PhoneNumberManager &phoneNumberManager)
    : _sender(sender), _targetNumber(targetNumber), _reader(reader), _mainPowerCheck(mainPowerCheck),
      _batteryProcessor(batteryProcessor), _tempHumidityProcessor(tempHumidityProcessor), 
      _configManager(configManager), _phoneNumberManager(phoneNumberManager) {}

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
    else if (textUpper == "CLEAR") {
        handleClearCommand(sms.number);
    }
    else if (textUpper == "HELP") {
        handleHelpCommand(sms.number);
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
    String configMsg = _configManager.getAllParams();
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
    paramName.toUpperCase();
    String valueStr = trimmed.substring(spaceIdx + 1);
    valueStr.trim();

    if (valueStr.length() == 0) {
        _sender.send(senderNumber, "ERROR: CONFIG: missing value");
        return;
    }
    
    bool success = false;
    String confirmMsg;
    
    // Temperature thresholds
    if (paramName == "TEMP_HIGH") {
        float val = valueStr.toFloat();
        _configManager.setFloat(ConfigManager::Param::TEMP_HIGH, val);
        confirmMsg = "OK: Temp High set to " + String(val, 1) + "°C";
        success = true;
    } 
    else if (paramName == "TEMP_LOW") {
        float val = valueStr.toFloat();
        _configManager.setFloat(ConfigManager::Param::TEMP_LOW, val);
        confirmMsg = "OK: Temp Low set to " + String(val, 1) + "°C";
        success = true;
    } 
    // Humidity thresholds
    else if (paramName == "HUMIDITY_HIGH") {
        float val = valueStr.toFloat();
        _configManager.setFloat(ConfigManager::Param::HUMIDITY_HIGH, val);
        confirmMsg = "OK: Humidity High set to " + String(val, 1) + "%";
        success = true;
    } 
    else if (paramName == "HUMIDITY_LOW") {
        float val = valueStr.toFloat();
        _configManager.setFloat(ConfigManager::Param::HUMIDITY_LOW, val);
        confirmMsg = "OK: Humidity Low set to " + String(val, 1) + "%";
        success = true;
    } 
    // Sensor offsets (calibration)
    else if (paramName == "TEMP_OFFSET") {
        float val = valueStr.toFloat();
        _configManager.setFloat(ConfigManager::Param::TEMP_OFFSET, val);
        confirmMsg = "OK: Temp Offset set to " + String(val, 1) + "°C";
        success = true;
    } 
    else if (paramName == "HUMIDITY_OFFSET") {
        float val = valueStr.toFloat();
        _configManager.setFloat(ConfigManager::Param::HUMIDITY_OFFSET, val);
        confirmMsg = "OK: Humidity Offset set to " + String(val, 1) + "%";
        success = true;
    } 
    // Battery thresholds
    else if (paramName == "BAT_ADC_THRESHOLD") {
        int val = valueStr.toInt();
        _configManager.setInt(ConfigManager::Param::BAT_ADC_THRESHOLD, val);
        confirmMsg = "OK: Battery Threshold set to " + String(val);
        success = true;
    } 
    else if (paramName == "BAT_ADC_NEAR_EMPTY") {
        int val = valueStr.toInt();
        _configManager.setInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY, val);
        confirmMsg = "OK: Battery Near Empty set to " + String(val);
        success = true;
    } 
    // Power thresholds
    else if (paramName == "POWER_ADC_THRESHOLD") {
        int val = valueStr.toInt();
        _configManager.setInt(ConfigManager::Param::POWER_ADC_THRESHOLD, val);
        confirmMsg = "OK: Power Threshold set to " + String(val);
        success = true;
    }
    
    if (success) {
        log_i("[CMD] %s", confirmMsg.c_str());
        _sender.send(senderNumber, confirmMsg);
    } else {
        String errorMsg = "ERROR: Unknown parameter: " + paramName + "\nValid: TEMP_HIGH, TEMP_LOW, TEMP_OFFSET, HUMIDITY_HIGH, HUMIDITY_LOW, HUMIDITY_OFFSET, BAT_ADC_THRESHOLD, BAT_ADC_NEAR_EMPTY, POWER_ADC_THRESHOLD";
        log_e("[CMD] %s", errorMsg.c_str());
        _sender.send(senderNumber, errorMsg);
    }
}

void SMSProcessor::handleClearCommand(const String &senderNumber)
{
    log_i("[CMD] Clear alert flags received");
    
    // Reset alert flags in all processors
    _tempHumidityProcessor.resetAlertFlags();
    _batteryProcessor.resetAlertFlags();
    _mainPowerCheck.resetAlertFlags();
    
    log_i("[OK] All alert SMS sent flags cleared");
    _sender.send(senderNumber, "OK: All alert SMS flags cleared");
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

void SMSProcessor::handleHelpCommand(const String &senderNumber) {
    log_i("[CMD] Help command received");
    
    auto perm = _phoneNumberManager.getPermission(senderNumber);
    String helpMsg = "Available Commands:\n\n";
    
    // Commands accessible to both READ and ADMIN
    helpMsg += "STATUS - Device status\n";
    helpMsg += "LEVELS - Sensor levels\n";
    helpMsg += "CONFIG - View params\n";
    helpMsg += "LISTPHONES - List phones\n";
    helpMsg += "CLEAR - Reset alerts\n";
    
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
        helpMsg += "MUTE <i|n> - Mute alerts\n";
        helpMsg += "UNMUTE <i|n> - Unmute alerts\n";
        helpMsg += "\nLegend:\n";
        helpMsg += "<i|n>=index/number\n";
        helpMsg += "<p>=admin|read\n";
        helpMsg += "[a]=opt alias";
        
        helpMsg += "\n\nParams: TEMP_HIGH, TEMP_LOW,\n";
        helpMsg += "TEMP_OFFSET, HUMIDITY_HIGH,\n";
        helpMsg += "HUMIDITY_LOW, HUMIDITY_OFFSET,\n";
        helpMsg += "BAT_ADC_THRESHOLD,\n";
        helpMsg += "BAT_ADC_NEAR_EMPTY,\n";
        helpMsg += "POWER_ADC_THRESHOLD";
    }
    
    log_i("[CMD] Help displayed");
    _sender.send(senderNumber, helpMsg);
}

void SMSProcessor::handleMuteCommand(const String &rest, const String &senderNumber) {
    String param = rest;
    param.trim();
    
    if (param.length() == 0) {
        _sender.send(senderNumber, "ERROR: MUTE syntax: MUTE <index|number>");
        return;
    }
    
    if (_phoneNumberManager.muteNumber(param)) {
        log_i("[CMD] Phone muted: %s", param.c_str());
        _sender.send(senderNumber, "OK: Phone " + param + " muted (won't receive alerts)");
    } else {
        _sender.send(senderNumber, "ERROR: Phone not found or invalid index. Run LISTPHONES to see available entries");
    }
}

void SMSProcessor::handleUnmuteCommand(const String &rest, const String &senderNumber) {
    String param = rest;
    param.trim();
    
    if (param.length() == 0) {
        _sender.send(senderNumber, "ERROR: UNMUTE syntax: UNMUTE <index|number>");
        return;
    }
    
    if (_phoneNumberManager.unmuteNumber(param)) {
        log_i("[CMD] Phone unmuted: %s", param.c_str());
        _sender.send(senderNumber, "OK: Phone " + param + " unmuted (will receive alerts)");
    } else {
        _sender.send(senderNumber, "ERROR: Phone not found or invalid index. Run LISTPHONES to see available entries");
    }
}

