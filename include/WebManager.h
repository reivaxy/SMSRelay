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
class TemperatureHumidityProcessor;
class BatteryProcessor;
class MainPowerCheck;

class WebManager {
public:
    static constexpr size_t MAX_PEERS = 10;
    static constexpr size_t PEER_NAME_LEN = 11;
    static constexpr size_t PEER_TYPE_LEN = 11;
    static constexpr size_t MAX_HEADER_SIZE = 2048;

    struct Peer {
        IPAddress ip;
        char      name[PEER_NAME_LEN + 1];
        char      type[PEER_TYPE_LEN + 1];
        unsigned long timestamp;  // Local epoch time of last ping (0 if clock not set)
        bool      used;
    };

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

    // Get the web server URL (only valid when server is running)
    String getServerURL() const;

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

    // Generate HTML for the list of devices that pinged this server
    String generatePeersHTML();

    // Store the peer IP and name, or update the name if the IP is already known
    // Returns false if the table is full and the IP is new
    bool registerPeer(const IPAddress &ip, const String &name, const String &type);

    // True if the IP is in the known devices table
    bool isKnownPeer(const IPAddress &ip) const;

    // GET http://<ip>/api/status on a known device; returns HTTP code (<=0 on connection error)
    int fetchPeerStatus(const IPAddress &ip, String &body);

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
    Peer                     _peers[MAX_PEERS];
};

