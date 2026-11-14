#include <cassert>
#include <cstdio>

#include "ConfigManager.h"
#include "SMSSender.h"

// Isolate the sender policy from ESP32 NVS and the rest of the firmware.
static int smsDisabled = 0;
ConfigManager::ConfigManager() = default;
ConfigManager::~ConfigManager() = default;
int ConfigManager::getInt(Param param) {
    assert(param == Param::SMS_SEND_DISABLED);
    return smsDisabled;
}

class CaptureStream : public Stream {
public:
    std::string captured;
    void print(const String &text) override { captured += text.c_str(); }
};

int main() {
    TinyGsm modem;
    CaptureStream stream;
    ConfigManager config;
    SMSSender sender(modem, stream, config);

    smsDisabled = 1;
    assert(sender.send("+1111", "Status: OK"));
    assert(modem.sentMessages.size() == 1);
    assert(modem.sentMessages.back().text == "Status: OK");

    // The confirmation for disabling alerts must also be delivered.
    assert(sender.send("+1111", "OK: SMS Alerts Disabled set to YES (disabled)"));
    assert(modem.sentMessages.size() == 2);

    modem.clearSent();
    assert(sender.sendAlert("+1111", "Battery near empty"));
    assert(modem.sentMessages.empty());
    assert(sender.sendAlert("+1111", "Temperature high: 28\xC2\xB0" "C"));
    assert(stream.captured.empty());

    // Unicode replies must still use the real UCS-2/PDU path.
    assert(sender.send("+1111", "Temp: 25\xC2\xB0" "C"));
    assert(stream.captured.find("00B0") != std::string::npos);

    smsDisabled = 0;
    assert(sender.sendAlert("+1111", "Device powered on"));
    assert(modem.sentMessages.size() == 1);
    assert(modem.sentMessages.back().text == "Device powered on");

    modem.sendSMSResult = false;
    assert(!sender.sendAlert("+1111", "Main power lost"));
    smsDisabled = 1;
    assert(!sender.send("+1111", "Reply failed"));
    modem.clearSent();
    assert(sender.sendAlert("+1111", "Suppressed alert"));
    assert(modem.sentMessages.empty());

    assert(sender.send("CONSOLE", "Status: OK"));
    assert(sender.sendAlert("CONSOLE", "Test alert"));
    assert(modem.sentMessages.empty());

    std::puts("SMS policy regression tests passed");
}