#include "SMSForwarder.h"

SMSForwarder::SMSForwarder(SMSSender &sender, const String &targetNumber)
    : _sender(sender), _targetNumber(targetNumber) {}

bool SMSForwarder::forward(const ReceivedSMS &sms)
{
    unsigned long now = millis();
    bool sameNumber = (sms.number == _lastSenderNumber);
    bool withinWindow = (now - _lastForwardTime) < HEADER_SUPPRESS_MS;
    bool sendHeader = !(sameNumber && withinWindow);

    if (sendHeader) {
        log_i("[INFO] Forwarding SMS from %s to %s", sms.number.c_str(), _targetNumber.c_str());    
        String headerMessage = "Fwd from: " + sms.number + "\nTime: " + sms.timestamp;
        bool sent1 = _sender.send(_targetNumber, headerMessage);
        log_i("%s", sent1 ? "[OK] Header SMS forwarded" : "[ERROR] Failed to forward header SMS");
    } else {
        log_i("[INFO] Header suppressed (same sender within 10s)");
    }

    bool sent2 = _sender.send(_targetNumber, sms.text);
    if (sent2) {
        log_i("[OK] Body SMS forwarded");
        _lastSenderNumber = sms.number;
        _lastForwardTime  = now;
    } else {
        log_e("[ERROR] Failed to forward body SMS");
        _sender.send(_targetNumber, "ERROR: Failed to forward SMS #" + String(sms.index) + " from " + sms.number);
    }
    return sent2;
}
