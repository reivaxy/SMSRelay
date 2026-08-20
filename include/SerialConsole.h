#pragma once
#include <Arduino.h>

// Forward declarations
class SMSProcessor;

// Processes interactive commands typed into the Serial monitor.
// Call check() from loop(). Commands are line-buffered and case-insensitive.
// All commands are processed through SMSProcessor as if they were incoming SMS messages
// from a special CONSOLE sender with ADMIN permissions.
//
// Supported commands (see SMSProcessor for full list):
//   see HELP command or send SMS commands directly to the ADMIN phone numbers
class SerialConsole {
public:
    SerialConsole(SMSProcessor &processor);

    // Call from loop(). Reads available Serial bytes, processes complete lines.
    void check();

private:
    void processLine(const String &line);

    SMSProcessor &_processor;
    String        _inputBuffer;
};
