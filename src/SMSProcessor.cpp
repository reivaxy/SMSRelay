#include "SMSProcessor.h"
#include "TemperatureHumidityProcessor.h"
#include "BatteryProcessor.h"
#include "utilities.h"

SMSProcessor::SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader, 
                           MainPowerCheck &mainPowerCheck, TemperatureHumidityProcessor &tempHumidityProcessor)
    : _sender(sender), _targetNumber(targetNumber), _reader(reader), _mainPowerCheck(mainPowerCheck),
      _tempHumidityProcessor(tempHumidityProcessor) {}

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
    bool isBattery = (batAdcValue > 0 && batAdcValue < BatteryProcessor::BAT_ADC_THRESHOLD);
    String powerSource = isBattery ? "Battery" : "USB";
    String statusMsg = "Status:\nBattery: " + String(batAdcValue) + " (" + powerSource + ")\n";
#else
    String statusMsg = "Status:\nBattery: No ADC\n";
#endif

    int mainAdcValue = MainPowerCheck::readGPIO00ADC();
    String mainStatus = (mainAdcValue >= MainPowerCheck::POWER_ADC_THRESHOLD) ? "OK" : "LOW";
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
    String levelMsg = _tempHumidityProcessor.getStatus();
    log_i("[CMD] %s", levelMsg.c_str());
    _sender.send(_targetNumber, levelMsg);
}

