#include "SerialConsole.h"
#include "SMSProcessor.h"
#include "SMSReader.h"

SerialConsole::SerialConsole(SMSProcessor &processor)
    : _processor(processor) {}

void SerialConsole::check()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            _inputBuffer.trim();
            if (_inputBuffer.length() > 0) {
                processLine(_inputBuffer);
            }
            _inputBuffer = "";
        } else {
            _inputBuffer += c;
        }
    }
}

void SerialConsole::processLine(const String &line)
{
    // Create a fake ReceivedSMS object as if it came from a CONSOLE sender
    // SMSProcessor will handle authorization and command parsing
    ReceivedSMS consoleSMS;
    consoleSMS.number = "CONSOLE";
    consoleSMS.text = line;
    consoleSMS.textRaw = "";
    consoleSMS.timestamp = "CONSOLE";
    consoleSMS.index = 0;
    
    Serial.printf("[CONSOLE] Processing: %s\n", line.c_str());
    
    // Process through SMSProcessor as if it were an incoming SMS
    _processor.process(consoleSMS);
}

