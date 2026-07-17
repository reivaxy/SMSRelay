#pragma once
#include <Arduino.h>
#include "SMSReader.h"
#include "SMSSender.h"
#include "MainPowerCheck.h"
#include "PhoneNumberManager.h"

// Forward declarations
class TemperatureHumidityProcessor;
class BatteryProcessor;
class ConfigManager;
class AlertManager;
class ClockManager;
class OTAManager;

// Handles command SMS received from authorized phone numbers.
// Supported commands:
//   READ/ADMIN Permission:
//   "STATUS" - get device status
//   "LEVELS" - get sensor levels
//   "CONFIG" - get all parameters and their current values
//   "LISTPHONES" - list all authorized phone numbers
//   "CLEAR" - reset all alert SMS sent flags
//   "HELP" - display list of available commands
//
//   ADMIN Permission Only:
//   "FOR:<number> <message>" - forward message
//   "LIST" - list stored messages
//   "READ <index>" - read message at index
//   "DELETE <index>" - delete message at index
//   "CONFIG <param> <value>" - set a threshold parameter
//   "ADDPHONE <number> admin|read" - add authorized phone number
//   "REMOVEPHONE <number>" - remove authorized phone number
//   "MUTE <number>" - mute phone number from receiving alerts
//   "UNMUTE <number>" - unmute phone number to receive alerts
//   "OTA" - start OTA firmware update (requires clear alerts)
class SMSProcessor {
public:
    SMSProcessor(SMSSender &sender, const String &targetNumber, SMSReader &reader, 
                 MainPowerCheck &mainPowerCheck, BatteryProcessor &batteryProcessor, TemperatureHumidityProcessor &tempHumidityProcessor,
                 ConfigManager &configManager, PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, ClockManager &clockManager,
                 OTAManager &otaManager);

    // Processes a command SMS. The original SMS is always considered handled
    // (caller should delete it regardless of individual command success).
    void process(const ReceivedSMS &sms);


private:
    void handleForCommand(const String &rest, const String &senderNumber);
    void handleStatusCommand(const String &senderNumber);
    void handleListCommand(int skipIndex, const String &senderNumber);
    void handleReadCommand(int index, const String &senderNumber);
    void handleDeleteCommand(int index, const String &senderNumber);
    void handleLevelCommand(const String &senderNumber);
    void handleReadConfigCommand(const String &senderNumber);
    void handleWriteConfigCommand(const String &rest, const String &senderNumber);
    void handleClearCommand(const String &senderNumber);
    void handleAddPhoneCommand(const String &rest, const String &senderNumber);
    void handleRemovePhoneCommand(const String &rest, const String &senderNumber);
    void handleListPhonesCommand(const String &senderNumber);
    void handleListAlertsCommand(const String &senderNumber);
    void handleHelpCommand(const String &senderNumber);
    void handleMuteCommand(const String &rest, const String &senderNumber);
    void handleUnmuteCommand(const String &rest, const String &senderNumber);
    void handleACKCommand(const String &rest, const String &senderNumber);
    void handleOTACommand(const String &senderNumber);
    
    // Check if sender has required permission level
    bool hasPermission(const String &senderNumber, PhoneNumberManager::Permission required);
    
    // Send permission denied message
    void sendPermissionDenied(const String &senderNumber);

    SMSSender                       &_sender;
    String                           _targetNumber;
    SMSReader                        &_reader;
    MainPowerCheck                   &_mainPowerCheck;
    BatteryProcessor                 &_batteryProcessor;
    TemperatureHumidityProcessor     &_tempHumidityProcessor;
    ConfigManager                    &_configManager;
    PhoneNumberManager               &_phoneNumberManager;
    AlertManager                     &_alertManager;
    ClockManager                     &_clockManager;
    OTAManager                       &_otaManager;
};
