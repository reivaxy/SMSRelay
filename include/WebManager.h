#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include "ModuleManager.h"

// Forward declarations
class ConfigManager;
class SMSSender;
class PhoneNumberManager;
class AlertManager;
class Modem;
class ClockManager;
class TemperatureHumidityProcessor;
class BatteryProcessor;
class MainPowerCheck;

class WebManager {
public:
    static constexpr size_t MAX_HEADER_SIZE = 2048;
    static constexpr const char *AP_SSID = "SMSRelay";

    WebManager(ConfigManager &configManager, SMSSender &sender, PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, Modem &modem, ClockManager &clockManager,
               TemperatureHumidityProcessor &tempHumidityProcessor, BatteryProcessor &batteryProcessor, MainPowerCheck &mainPowerCheck);
    ~WebManager();

    // Start web server and connect to home WiFi
    // Returns true if successful, false otherwise
    // Sends the access URL to the requester via SMS
    bool init(const String &requesterNumber);

    // Stop web server and disconnect from WiFi
    void stop();

    // Check if web server is currently running
    bool isRunning() const;

    // True when the fallback access point is up
    bool isApActive() const { return _apActive; }

    // Get the web server URL (only valid when server is running)
    String getServerURL() const;

    // Get the numbered list of known module names
    String getModuleList() const;

    // Fetch raw status for a module using its one-based list number
    int fetchModuleStatusByNumber(int number, String &body);

    // Call this from loop() to handle incoming web requests
    void check();

private:
    // Handle HTTP request
    void handleClient(WiFiClient client);

    // Parse HTTP request line
    bool parseHttpRequest(const String &request, String &method, String &path, String &query);

    // Read the request body (bounded by Content-Length, maxSize and a 1s timeout)
    String readRequestBody(WiFiClient &client, const String &request, int maxSize);

    // Send a complete HTTP response, then close the connection
    void sendResponse(WiFiClient &client, const String &status, const String &contentType, const String &body);

    // Watch the home WiFi link: on loss, alert and serve the web interface from the embedded access point
    void checkWiFiLink();
    void startAccessPoint();
    void stopAccessPoint();

    // Access point password from preferences (generated and saved if not set)
    String getAccessPointPassword();

    // Generate HTML form with all parameters
    String generatePageStart(const String &title, const String &heading);
    String generateHomePage();
    String generateConfigPage();

    // Handle form submission to update parameters
    String handleFormSubmit(const String &postData);
    
    // Generate a random 10-character alphanumeric token
    String generateRandomToken();
    
    // Generate HTML for pending alerts display
    String generateAlertsHTML();
    
    // Generate HTML for current sensor levels
    String generateLevelsHTML();

    WiFiServer              *_server;
    ConfigManager           &_configManager;
    SMSSender               &_sender;
    PhoneNumberManager      &_phoneNumberManager;
    AlertManager            &_alertManager;
    Modem                   &_modem;
    ClockManager            &_clockManager;
    TemperatureHumidityProcessor &_tempHumidityProcessor;
    BatteryProcessor        &_batteryProcessor;
    MainPowerCheck          &_mainPowerCheck;
    bool                     _isRunning;
    String                   _requesterNumber;
    String                   _serverURL;
    String                   _accessToken;
    ModuleManager            _moduleManager;
    bool                     _apActive = false;      // Embedded access point is up because home WiFi was lost
    unsigned long            _wifiLostSince = 0;     // millis() when the home WiFi link was first seen down (0 = link up)
    unsigned long            _lastApRecheck = 0;     // millis() of the last home WiFi check while the AP is open
};
