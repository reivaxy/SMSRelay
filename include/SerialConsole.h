#pragma once
#include <Arduino.h>
#include "SMSReader.h"
#include "SMSForwarder.h"

// Processes interactive commands typed into the Serial monitor.
// Call check() from loop(). Commands are line-buffered and case-insensitive.
//
// Supported commands:
//   list      — log all stored messages (index, sender, preview)
//   read X    — log the SMS stored at modem index X
//   delete X  — delete the SMS at modem index X
//   forward X — forward the SMS at modem index X to the configured target number
class SerialConsole {
public:
    SerialConsole(SMSReader &reader, SMSForwarder &forwarder);

    // Call from loop(). Reads available Serial bytes, processes complete lines.
    void check();

private:
    void processLine(const String &line);
    void handleList();
    void handleRead(int index);
    void handleDelete(int index);
    void handleForward(int index);

    SMSReader    &_reader;
    SMSForwarder &_forwarder;
    String        _inputBuffer;
};
