#pragma once
#include <Arduino.h>
#include <vector>
#include "SMSSender.h"
#include "ConfigManager.h"
#include "PhoneNumberManager.h"
#include "ClockManager.h"

// Manages alert sending with acknowledgment (ACK) system.
// - Each alert gets a unique 3-digit random code
// - Alerts are resent every 5 minutes (configurable) until ACKed
// - Tracks ACKs per phone number and notifies others when alert is ACKed
// - Respects muted phone numbers (won't send to muted but they can ACK)
// - Stores timestamp when alert is triggered (if ClockManager is available)
class AlertManager {
public:
    // Alert priority levels (optional for future enhancement)
    enum class AlertLevel {
        PRIORITY_LOW,    // Can be muted
        PRIORITY_NORMAL, // Can be muted
        PRIORITY_HIGH    // Should always be sent? (future feature)
    };

    AlertManager(SMSSender &sender, ConfigManager &configManager, PhoneNumberManager &phoneNumberManager, ClockManager &clockManager);
    ~AlertManager();

    // Send or queue an alert with automatic code generation
    // cause: brief reason for alert (e.g., "Temperature HIGH at 28.5°C", "Battery LOW at 1500")
    // Returns the 3-digit alert code
    String sendAlert(const String &title, const String &message, const String &cause = "", AlertLevel level = AlertLevel::PRIORITY_NORMAL);

    // Handle incoming ACK from a phone number (format: "ACK <code>")
    // Returns true if ACK was processed successfully
    bool handleACK(const String &senderNumber, const String &code);

    // Called periodically from loop() to resend pending alerts and manage timeouts
    void check();

    // Clear all pending alerts (for debugging/reset)
    void clearAll();

    // Get list of pending alerts (for debugging)
    String getPendingAlertsList();

private:
    struct Alert {
        String code;           // 3-digit unique code
        String title;          // Alert title/type
        String message;        // Alert message text
        String cause;          // What triggered the alert (for LISTALERTS display)
        unsigned long created; // When alert was created (millis)
        unsigned long createdTime; // Unix timestamp when alert was triggered
        String createdTimeStr; // Formatted date/time when alert was triggered
        unsigned long lastSent;// When alert was last sent (millis)
        AlertLevel level;      // Alert priority level
        std::vector<String> ackedBy; // List of phone numbers that ACKed
    };

    // Generate a unique 3-digit code that doesn't already exist
    String generateUniqueCode();

    // Send alert to all non-muted numbers, update lastSent time
    void sendAlertToAll(Alert &alert);

    // Send ACK notification to all numbers except the one that sent the ACK
    void notifyACKToOthers(const Alert &alert, const String &ackSenderNumber);

    // Find alert by code, returns index or -1 if not found
    int findAlertByCode(const String &code);

    SMSSender              &_sender;
    ConfigManager          &_configManager;
    PhoneNumberManager     &_phoneNumberManager;
    ClockManager           &_clockManager;
    std::vector<Alert>     _pendingAlerts;
    unsigned long          _lastCheck;
};
