#pragma once
#include <Arduino.h>
#include "SMSReader.h"
#include "SMSSender.h"
#include "MainPowerCheck.h"

// Forward declarations
class TemperatureHumidityProcessor;
class ConfigManager;

// Handles command SMS received from SMS_TARGET.
// Supported commands: 
//   "FOR:<number> <message>" - forward message
//   "STATUS" - get device status
//   "LIST" - list stored messages
//   "READ <index>" - read message at index
//   "DELETE <index>" - delete message at index
//   "LEVELS" - get sensor levels
//   "THRESHOLDS" or "THRESHOLD?" - get all thresholds
//   "CONFIG <param> <value>" - set a threshold parameter
class SMSProcessor {
public:
    SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader, 
                 MainPowerCheck &mainPowerCheck, TemperatureHumidityProcessor &tempHumidityProcessor,
                 ConfigManager &configManager);

    // Processes a command SMS. The original SMS is always considered handled
    // (caller should delete it regardless of individual command success).
    void process(const ReceivedSMS &sms);


private:
    void handleForCommand(const String &rest);
    void handleStatusCommand();
    void handleListCommand(int skipIndex);
    void handleReadCommand(int index);
    void handleDeleteCommand(int index);
    void handleLevelCommand();
    void handleReadConfigCommand();
    void handleWriteConfigCommand(const String &rest);

    SMSSender                       &_sender;
    String                           _targetNumber;
    SMSReader                        &_reader;
    MainPowerCheck                   &_mainPowerCheck;
    TemperatureHumidityProcessor     &_tempHumidityProcessor;
    ConfigManager                    &_configManager;
};
