#include "SMSProcessor.h"
#include "TemperatureHumidityProcessor.h"
#include "ConfigManager.h"
#include "BatteryProcessor.h"
#include "MainPowerCheck.h"
#include "utilities.h"

SMSProcessor::SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader, 
                           MainPowerCheck &mainPowerCheck, BatteryProcessor &batteryProcessor, TemperatureHumidityProcessor &tempHumidityProcessor,
                           ConfigManager &configManager)
    : _sender(sender), _targetNumber(targetNumber), _reader(reader), _mainPowerCheck(mainPowerCheck),
      _batteryProcessor(batteryProcessor), _tempHumidityProcessor(tempHumidityProcessor), _configManager(configManager) {}

void SMSProcessor::process(const ReceivedSMS &sms)
{
    String textUpper = sms.text;
    textUpper.trim();
    textUpper.toUpperCase();

    if (textUpper.startsWith("FOR:")) {
        handleForCommand(sms.text.substring(4));
    } else if (textUpper == "STATUS") {
        handleStatusCommand();
    } else if (textUpper == "LEVELS") {
        handleLevelCommand();
    } else if (textUpper == "LIST") {
        handleListCommand(sms.index);
    } else if (textUpper == "CONFIG") {
        handleReadConfigCommand();
    } else if (textUpper.startsWith("CONFIG ")) {
        handleWriteConfigCommand(sms.text.substring(7));
    } else if (textUpper.startsWith("READ ")) {
        int index = sms.text.substring(5).toInt();
        if (index > 0) {
            handleReadCommand(index);
        } else {
            _sender.send(_targetNumber, "ERROR: read: invalid index");
        }
    } else if (textUpper.startsWith("DELETE ")) {
        int index = sms.text.substring(7).toInt();
        if (index > 0) {
            handleDeleteCommand(index);
        } else {
            _sender.send(_targetNumber, "ERROR: delete: invalid index");
        }
    } else if (textUpper == "CLEAR") {
        handleClearCommand();
    }
}

void SMSProcessor::handleForCommand(const String &rest)
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


void SMSProcessor::handleReadCommand(int index)
{
    log_i("[CMD] Read query for index %d", index);
    ReceivedSMS sms;
    if (!_reader.readAt(index, sms)) {
        _sender.send(_targetNumber, "ERROR: No message at index " + String(index));
        return;
    }
    String msg = "[" + String(sms.index) + "] From: " + sms.number +
                 "\nTime: " + sms.timestamp +
                 "\n" + sms.text;
    _sender.send(_targetNumber, msg);
}

void SMSProcessor::handleDeleteCommand(int index)
{
    log_i("[CMD] Delete query for index %d", index);
    _reader.deleteMessage(index);
    _sender.send(_targetNumber, "OK: Message " + String(index) + " deleted");
}

void SMSProcessor::handleStatusCommand()
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
    _sender.send(_targetNumber, statusMsg);
}

void SMSProcessor::handleListCommand(int skipIndex)
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
        _sender.send(_targetNumber, "No messages stored");
        return;
    }

    _sender.send(_targetNumber, response);
}

void SMSProcessor::handleLevelCommand()
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
    _sender.send(_targetNumber, levelMsg);
}

void SMSProcessor::handleReadConfigCommand()
{
    log_i("[CMD] Config query received");
    String configMsg = _configManager.getAllParams();
    log_i("[CMD] %s", configMsg.c_str());
    _sender.send(_targetNumber, configMsg);
}

void SMSProcessor::handleWriteConfigCommand(const String &rest)
{
    String trimmed = rest;
    trimmed.trim();
    
    // Parse: "CONFIG <param> <value>"
    // Format: parameter name (case insensitive) followed by value
    
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx <= 0) {
        _sender.send(_targetNumber, "ERROR: CONFIG syntax: CONFIG <param> <value>");
        return;
    }
    
    String paramName = trimmed.substring(0, spaceIdx);
    paramName.toUpperCase();
    String valueStr = trimmed.substring(spaceIdx + 1);
    valueStr.trim();

    if (valueStr.length() == 0) {
        _sender.send(_targetNumber, "ERROR: CONFIG: missing value");
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
        _sender.send(_targetNumber, confirmMsg);
    } else {
        String errorMsg = "ERROR: Unknown parameter: " + paramName + "\nValid: TEMP_HIGH, TEMP_LOW, TEMP_OFFSET, HUMIDITY_HIGH, HUMIDITY_LOW, HUMIDITY_OFFSET, BAT_ADC_THRESHOLD, BAT_ADC_NEAR_EMPTY, POWER_ADC_THRESHOLD";
        log_e("[CMD] %s", errorMsg.c_str());
        _sender.send(_targetNumber, errorMsg);
    }
}

void SMSProcessor::handleClearCommand()
{
    log_i("[CMD] Clear alert flags received");
    
    // Reset alert flags in all processors
    _tempHumidityProcessor.resetAlertFlags();
    _batteryProcessor.resetAlertFlags();
    _mainPowerCheck.resetAlertFlags();
    
    log_i("[OK] All alert SMS sent flags cleared");
    _sender.send(_targetNumber, "OK: All alert SMS flags cleared");
}

