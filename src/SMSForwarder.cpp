#include "SMSForwarder.h"

SMSForwarder::SMSForwarder(SMSSender &sender, const String &targetNumber)
    : _sender(sender), _targetNumber(targetNumber) {}

bool SMSForwarder::forward(const ReceivedSMS &sms)
{
    String headerMessage = "Fwd from: " + sms.number + "\nTime: " + sms.timestamp;
    bool sent1 = _sender.send(_targetNumber, headerMessage);
    log_i("%s", sent1 ? "[OK] Header SMS forwarded" : "[ERROR] Failed to forward header SMS");

    bool sent2 = _sender.send(_targetNumber, sms.text);
    if (sent2) {
        log_i("[OK] Body SMS forwarded");
    } else {
        log_i("[ERROR] Failed to forward body SMS");
        _sender.send(_targetNumber, "ERROR: Failed to forward SMS from " + sms.number);
    }
    return sent2;
}
