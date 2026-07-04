#pragma once
#include <Arduino.h>
#include "SMSReader.h"
#include "SMSSender.h"

// Handles command SMS received from SMS_TARGET.
// Supported commands: "FOR:<number> <message>", "STATUS", "LIST", 
// "READ <index>", "DELETE <index>".
class SMSProcessor {
public:
    SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader);

    // Processes a command SMS. The original SMS is always considered handled
    // (caller should delete it regardless of individual command success).
    void process(const ReceivedSMS &sms);


private:
    void handleForCommand(const String &rest);
    void handleStatusCommand();
    void handleListCommand(int skipIndex);
    void handleReadCommand(int index);
    void handleDeleteCommand(int index);

    SMSSender &_sender;
    String     _targetNumber;
    SMSReader &_reader;
};
