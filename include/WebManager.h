#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>

// Forward declarations
class ConfigManager;
class SMSSender;
class PhoneNumberManager;

class WebManager {
public:
    WebManager(ConfigManager &configManager, SMSSender &sender, PhoneNumberManager &phoneNumberManager);
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

    WiFiServer              *_server;
    ConfigManager           &_configManager;
    SMSSender               &_sender;
    PhoneNumberManager      &_phoneNumberManager;
    bool                     _isRunning;
    String                   _requesterNumber;
    String                   _serverURL;
};

