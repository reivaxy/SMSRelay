#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "SMSSender.h"
#include "ConfigManager.h"
#include "PhoneNumberManager.h"
#include "AlertManager.h"
#include "Modem.h"

// Forward declarations
class MainPowerCheck;

// Manages OTA firmware updates via WiFi and file upload.
// Features:
// - Connects to configured WiFi SSID/Password
// - Hosts a simple web server with file upload form
// - Sends SMS with URL to access the upload page
// - Performs firmware update from uploaded file
// - Auto-restarts device after successful update
// - WiFi disabled until next OTA request
// - 90-second timeout for file upload initiation; if upload doesn't start, OTA is cancelled
class OTAManager {
public:
    // OTA upload timeout: 90 seconds from init() to first byte of upload
    static const unsigned long OTA_UPLOAD_TIMEOUT_MS = 90000;

    OTAManager(SMSSender &sender, const String &targetNumber, ConfigManager &configManager, 
               PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, MainPowerCheck &mainPowerCheck, Modem &modem);
    ~OTAManager();

    // Initialize OTA manager (connects to WiFi, starts web server)
    // Returns true if successfully connected and server started
    bool init();

    // Stop OTA mode (disconnect WiFi, stop web server)
    void stop();

    // Called periodically from loop() to handle web server requests
    void check();

    // Check if OTA is currently active
    bool isActive() const { return _isActive; }

    // Get the local IP address when in OTA mode
    String getLocalIP() const;

    // Set the requester's phone number (used internally by SMSProcessor)
    void setRequesterNumber(const String &number) { _requesterNumber = number; }

private:
    // Web server handlers
    void handleRoot();
    void handleUpload();
    void handleFileUpload();
    void handleStatus();
    void handleCancel();

    // Generate HTML page with upload form
    String generateUploadPage();

    // Generate a random 10-character alphanumeric token
    String generateRandomToken();

    // Connect to configured WiFi
    bool connectToWiFi();

    // Start web server
    bool startWebServer();

    // Send URL via SMS to requesting number
    void sendURLtoSMS();

    // Restart the device
    void restartDevice();

    SMSSender              &_sender;
    String                 _targetNumber;
    ConfigManager          &_configManager;
    PhoneNumberManager     &_phoneNumberManager;
    AlertManager           &_alertManager;
    MainPowerCheck         &_mainPowerCheck;
    Modem                  &_modem;
    
    WebServer              *_webServer;
    bool                   _isActive;
    unsigned long          _startTime;
    bool                   _uploadStarted;
    String                 _requesterNumber;
    size_t                 _uploadedBytes;
    bool                   _updateInProgress;
    bool                   _updateCompleted;
    String                 _otaAccessToken;
};
