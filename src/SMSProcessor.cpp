#include "SMSProcessor.h"
#include "utilities.h"

SMSProcessor::SMSProcessor(SMSSender &sender, const String &targetNumber)
    : _sender(sender), _targetNumber(targetNumber) {}

void SMSProcessor::process(const ReceivedSMS &sms)
{
    String textUpper = sms.text;
    textUpper.trim();
    textUpper.toUpperCase();

    if (textUpper.startsWith("FOR:")) {
        handleForCommand(sms.text.substring(4));
    } else if (textUpper == "STATUS") {
        handleStatusCommand();
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

void SMSProcessor::handleStatusCommand()
{
    log_i("[CMD] Status query received");

#ifdef BOARD_BAT_ADC_PIN
    int adcValue = readBatADC();
    bool isBattery = (adcValue > 0 && adcValue < BAT_ADC_THRESHOLD);
    String powerSource = isBattery ? "Battery" : "USB";

    String statusMsg = "Status: ADC=" + String(adcValue) +
                       " Threshold=" + String(BAT_ADC_THRESHOLD) +
                       " Power=" + powerSource;
#else
    String statusMsg = "Status: No battery ADC";
#endif

    log_i("[CMD] %s", statusMsg.c_str());
    _sender.send(_targetNumber, statusMsg);
}

int SMSProcessor::readBatADC()
{
#ifdef BOARD_BAT_ADC_PIN
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += analogRead(BOARD_BAT_ADC_PIN);
        delay(10);
    }
    return sum / 10;
#else
    return 0;
#endif
}

bool SMSProcessor::isPoweredByBattery()
{
    int adcValue = readBatADC();
    log_i("Battery ADC: %d (threshold: %d)", adcValue, BAT_ADC_THRESHOLD);
    return adcValue > 0 && adcValue < BAT_ADC_THRESHOLD;
}
