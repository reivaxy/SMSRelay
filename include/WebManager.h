#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>

// Forward declarations
class ConfigManager;
class SMSSender;
class PhoneNumberManager;
class AlertManager;
class Modem;
class ClockManager;

class WebManager {
public:
    WebManager(ConfigManager &configManager, SMSSender &sender, PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, Modem &modem, ClockManager &clockManager);
    ~WebManager();

    // Start web server and connect to home WiFi
    // Returns true if successful, false otherwise
    // Sends the access URL to the requester via SMS
    bool init(const String &requesterNumber);

    // Stop web server and disconnect from WiFi
    void stop();

    // Check if web server is currently running
    bool isRunning() const;

    // Call this from loop() to handle incoming web requests
    void check();

private:
    // Handle HTTP request
    void handleClient(WiFiClient client);

    // Parse HTTP request line
    bool parseHttpRequest(const String &request, String &method, String &path, String &query);

    // Generate HTML form with all parameters
    String generateHTMLForm();

    // Handle form submission to update parameters
    String handleFormSubmit(const String &postData);
    
    // Generate a random 10-character alphanumeric token
    String generateRandomToken();
    
    // Generate HTML for pending alerts display
    String generateAlertsHTML();

    WiFiServer              *_server;
    ConfigManager           &_configManager;
    SMSSender               &_sender;
    PhoneNumberManager      &_phoneNumberManager;
    AlertManager            &_alertManager;
    Modem                   &_modem;
    ClockManager            &_clockManager;
    bool                     _isRunning;
    String                   _requesterNumber;
    String                   _serverURL;
    String                   _accessToken;
};

