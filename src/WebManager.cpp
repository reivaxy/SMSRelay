#include "WebManager.h"
#include "ConfigManager.h"
#include "SMSSender.h"
#include "PhoneNumberManager.h"
#include "AlertManager.h"
#include "ClockManager.h"
#include "Modem.h"
#include "TemperatureHumidityProcessor.h"
#include "BatteryProcessor.h"
#include "MainPowerCheck.h"
#include <ctime>
#include <ArduinoJson.h>

static String htmlEscape(const String &s) {
    String out = "";
    for (int i = 0; i < (int)s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

WebManager::WebManager(ConfigManager &configManager, SMSSender &sender, PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, Modem &modem, ClockManager &clockManager,
                       TemperatureHumidityProcessor &tempHumidityProcessor, BatteryProcessor &batteryProcessor, MainPowerCheck &mainPowerCheck)
    : _server(nullptr), _configManager(configManager), _sender(sender), _phoneNumberManager(phoneNumberManager), _alertManager(alertManager), _modem(modem), _clockManager(clockManager),
      _tempHumidityProcessor(tempHumidityProcessor), _batteryProcessor(batteryProcessor), _mainPowerCheck(mainPowerCheck),
      _isRunning(false), _requesterNumber(""), _serverURL(""), _accessToken("") {
    for (size_t i = 0; i < MAX_PEERS; i++) {
        _peers[i].used = false;
        _peers[i].name[0] = '\0';
        _peers[i].type[0] = '\0';
        _peers[i].timestamp = 0;
    }
}

bool WebManager::registerPeer(const IPAddress &ip, const String &name, const String &type) {
    int freeSlot = -1;
    unsigned long now = _clockManager.isInitialized() ? _clockManager.getCurrentTime() : 0;
    for (size_t i = 0; i < MAX_PEERS; i++) {
        if (_peers[i].used && _peers[i].ip == ip) {
            strlcpy(_peers[i].name, name.c_str(), sizeof(_peers[i].name));
            strlcpy(_peers[i].type, type.c_str(), sizeof(_peers[i].type));
            _peers[i].timestamp = now;
            return true;
        }
        if (!_peers[i].used && freeSlot < 0) {
            freeSlot = (int)i;
        }
    }
    if (freeSlot < 0) {
        log_w("[WEB] Peer table full, ignoring %s", ip.toString().c_str());
        return false;
    }
    _peers[freeSlot].used = true;
    _peers[freeSlot].ip = ip;
    strlcpy(_peers[freeSlot].name, name.c_str(), sizeof(_peers[freeSlot].name));
    strlcpy(_peers[freeSlot].type, type.c_str(), sizeof(_peers[freeSlot].type));
    _peers[freeSlot].timestamp = now;
    return true;
}

static String formatPeerTimestamp(unsigned long epoch) {
    if (epoch == 0) {
        return "-";
    }
    time_t t = (time_t)epoch;
    struct tm *timeinfo = gmtime(&t);  // Epoch already includes local offsets
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buffer);
}

String WebManager::generatePeersHTML() {
    String rows = "";
    for (size_t i = 0; i < MAX_PEERS; i++) {
        if (!_peers[i].used) continue;
        String ip = _peers[i].ip.toString();
        rows += "                <tr>";
        rows += "<td>" + htmlEscape(String(_peers[i].name));
        if (_peers[i].type[0] != '\0') {
            rows += "<br><span class=\"dev-type\">(" + htmlEscape(String(_peers[i].type)) + ")</span>";
        }
        rows += "</td>";
        rows += "<td>" + formatPeerTimestamp(_peers[i].timestamp) + "</td>";
        rows += "<td><a class=\"btn btn-sm\" href=\"http://" + ip + "/\" target=\"_blank\" rel=\"noopener\">home</a></td>";
        rows += "<td><button type=\"button\" class=\"btn-sm status-btn\" data-ip=\"" + ip + "\">status</button></td>";
        rows += "</tr>\n";
    }
    if (rows.length() == 0) {
        return "            <div class=\"empty\">No devices recorded</div>\n";
    }
    String html = "            <table class=\"devices\">\n";
    html += "                <tr><th>Name</th><th>Last ping</th><th>Home</th><th>Status</th></tr>\n";
    html += rows;
    html += "            </table>\n";
    html += "            <dialog id=\"statusDialog\">\n";
    html += "                <pre id=\"statusBody\"></pre>\n";
    html += "                <button type=\"button\" id=\"statusQuit\">Quit</button>\n";
    html += "            </dialog>\n";
    return html;
}

bool WebManager::isKnownPeer(const IPAddress &ip) const {
    for (size_t i = 0; i < MAX_PEERS; i++) {
        if (_peers[i].used && _peers[i].ip == ip) {
            return true;
        }
    }
    return false;
}

int WebManager::fetchPeerStatus(const IPAddress &ip, String &body) {
    // Plain WiFiClient request (HTTPClient.h pulls mbedtls headers that clash with the -DX build flag)
    const int maxResponseSize = 4096;
    WiFiClient peer;
    if (!peer.connect(ip, 80, 2000)) {
        return -1;
    }

    peer.print("GET /api/status HTTP/1.1\r\nHost: " + ip.toString() + "\r\nConnection: close\r\n\r\n");

    String response = "";
    unsigned long deadline = millis() + 3000;
    while ((peer.connected() || peer.available()) && millis() < deadline && (int)response.length() < maxResponseSize) {
        if (peer.available()) {
            response += (char)peer.read();
        } else {
            delay(1);
        }
    }
    peer.stop();

    // Status line: "HTTP/1.1 200 OK"
    int firstSpace = response.indexOf(' ');
    int headersEnd = response.indexOf("\r\n\r\n");
    if (firstSpace < 0 || headersEnd < 0) {
        return -2;
    }
    int code = response.substring(firstSpace + 1, firstSpace + 4).toInt();
    body = response.substring(headersEnd + 4);
    return code;
}

WebManager::~WebManager() {
    stop();
}

bool WebManager::init(const String &requesterNumber) {
    if (_isRunning) {
        log_w("[WEB] Web server already running");
        return false;
    }

    _requesterNumber = requesterNumber;

    // Get WiFi credentials from config
    String ssid = _configManager.getStringParam(ConfigManager::Param::WIFI_SSID);
    String password = _configManager.getStringParam(ConfigManager::Param::WIFI_PASSWORD);

    if (ssid.length() == 0) {
        log_e("[WEB] WiFi SSID not configured");
        if (requesterNumber.length() > 0) {
            _sender.send(requesterNumber, "ERROR: WiFi SSID not configured. Use: CONFIG WIFI_SSID <ssid>");
        }
        return false;
    }

    log_i("[WEB] Connecting to WiFi: %s", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for WiFi connection (max 10 seconds)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        log_e("[WEB] Failed to connect to WiFi");
        if (requesterNumber.length() > 0) {
            _sender.send(requesterNumber, "ERROR: Failed to connect to WiFi. Check SSID and password.");
        }
        return false;
    }

    log_i("[WEB] WiFi connected. IP: %s", WiFi.localIP().toString().c_str());

    // Generate random access token
    _accessToken = generateRandomToken();
    log_i("[WEB] Generated access token: %s", _accessToken.c_str());

    // Create web server on port 80
    _server = new WiFiServer(80);
    _server->begin();
    _isRunning = true;

    // Build server URLs with access token
    String ipURL = "http://" + WiFi.localIP().toString() + "/" + _accessToken + "/";
    _serverURL = "http://" + WiFi.localIP().toString() + "/" + _accessToken + "/";
    log_i("[WEB] Web server started at: %s", _serverURL.c_str());

    // Send URL to requester (none when started automatically at boot)
    if (requesterNumber.length() > 0) {
        _sender.send(requesterNumber, "OK: Web interface started\n" + _serverURL);
    }

    return true;
}

void WebManager::stop() {
    if (!_isRunning) {
        return;
    }

    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }

    WiFi.disconnect(true);  // true = turn off WiFi radio
    _isRunning = false;
    log_i("[WEB] Web server stopped and WiFi disconnected");
}

bool WebManager::isRunning() const {
    return _isRunning;
}

String WebManager::getServerURL() const {
    return _serverURL;
}

void WebManager::check() {
    if (!_isRunning || !_server) {
        return;
    }

    // Check for incoming client
    WiFiClient client = _server->available();
    if (client) {
        handleClient(client);
    }
}

void WebManager::handleClient(WiFiClient client) {
    log_i("[WEB] Client connected");
    
    // Read HTTP request
    String request = "";
    unsigned long timeout = millis() + 2000;  // 2 second timeout
    
    while (client.connected() && millis() < timeout && request.length() < MAX_HEADER_SIZE) {
        if (client.available()) {
            char c = client.read();
            request += c;
            if (request.endsWith("\r\n\r\n")) {
                break;
            }
        } else {
            delay(1);
        }
    }

    if (request.length() == 0) {
        client.stop();
        return;
    }

    // Parse request
    String method, path, query;
    if (!parseHttpRequest(request, method, path, query)) {
        sendResponse(client, "400 Bad Request", "text/plain", "400 Bad Request");
        return;
    }

    log_i("[WEB] %s %s", method.c_str(), path.c_str());

    // Handle POST /api/ping (no access token required)
    // Body: {"IP":"<ip address>","name":"<device name>","type":"<device type>"} (name and type truncated to 11 chars)
    if (method == "POST" && path == "/api/ping") {
        String postBody = readRequestBody(client, request, 256);

        String status = "200 OK";
        String response = "SmsRelay";
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, postBody);
        String ipStr = doc["IP"] | "";
        String name = doc["name"] | "";
        String type = doc["type"] | "";
        IPAddress peerIp;
        if (err || !peerIp.fromString(ipStr)) {
            log_w("[WEB] Invalid ping request");
            status = "400 Bad Request";
            response = "Expected JSON with IP and name";
        } else {
            registerPeer(peerIp, name, type);
        }

        sendResponse(client, status, "text/plain", response);
        return;
    }

    // Handle POST /api/alerte (no access token required)
    // Body: {"module":"<name>","text":"<message>"}
    if (method == "POST" && path == "/api/alerte") {
        String postBody = readRequestBody(client, request, 1024);

        String status = "200 OK";
        String response;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, postBody);
        String module = doc["module"] | "";
        String text = doc["text"] | "";
        if (err || module.length() == 0 || text.length() == 0) {
            log_w("[WEB] Invalid alerte request");
            status = "400 Bad Request";
            response = "{\"status\":\"error\",\"message\":\"Expected JSON with module and text\"}";
        } else {
            String message = "Alert " + module + ": " + text;
            log_i("[WEB] %s", message.c_str());
            _alertManager.sendNotification("Alert " + module, message);
            response = "{\"status\":\"ok\"}";
        }

        sendResponse(client, status, "application/json", response);
        return;
    }

    // Check if path contains the access token
    if (!path.startsWith("/" + _accessToken)) {
        log_w("[WEB] Access denied - invalid token in path: %s", path.c_str());
        sendResponse(client, "403 Forbidden", "text/plain", "403 Forbidden");
        return;
    }

    // Remove token prefix from path for routing
    String routePath = path.substring(("/" + _accessToken).length());
    if (routePath.length() == 0 || routePath == "/") {
        routePath = "/";
    }

    // Handle GET /
    if (method == "GET" && (routePath == "/" || routePath == "")) {
        sendResponse(client, "200 OK", "text/html; charset=UTF-8", generateHomePage());
    }
    // Handle GET /config
    else if (method == "GET" && routePath == "/config") {
        sendResponse(client, "200 OK", "text/html; charset=UTF-8", generateConfigPage());
    }
    // Handle POST /api/config
    else if (method == "POST" && routePath == "/api/config") {
        String postBody = readRequestBody(client, request, 2048);
        sendResponse(client, "200 OK", "application/json", handleFormSubmit(postBody));
    }
    // Handle POST /api/restart
    else if (method == "POST" && routePath == "/api/restart") {
        log_i("[WEB] Restart request received");
        sendResponse(client, "200 OK", "application/json", "{\"status\":\"ok\",\"message\":\"Device restarting\"}");

        // Restart device after the response has been sent and the connection closed
        delay(500);
        _modem.restartDevice();
    }
    // Handle POST /api/weboff
    else if (method == "POST" && routePath == "/api/weboff") {
        log_i("[WEB] Web off request received");
        sendResponse(client, "200 OK", "application/json", "{\"status\":\"ok\",\"message\":\"Web server stopping\"}");

        // Stop web server after the response has been sent and the connection closed
        stop();
    }
    // Handle POST /api/ack
    else if (method == "POST" && routePath == "/api/ack") {
        String postBody = readRequestBody(client, request, 256);

        // Parse alert code from POST body: "code=123"
        String code = "";
        int codePos = postBody.indexOf("code=");
        if (codePos >= 0) {
            int endPos = postBody.indexOf('&', codePos);
            if (endPos < 0) endPos = postBody.length();
            code = postBody.substring(codePos + 5, endPos);
            code.trim();
        }
        
        String response;
        if (code.length() == 0) {
            log_w("[WEB] ACK request missing code");
            response = "{\"status\":\"error\",\"message\":\"Missing alert code\"}";
        } else {
            log_i("[WEB] ACK request for code: %s", code.c_str());
            if (_alertManager.handleACK(_requesterNumber, code)) {
                response = "{\"status\":\"ok\",\"message\":\"Alert acknowledged\"}";
            } else {
                response = "{\"status\":\"error\",\"message\":\"Unknown alert code\"}";
            }
        }
        
        sendResponse(client, "200 OK", "application/json", response);
        log_i("[WEB] ACK response sent");
    }
    // Handle GET /api/status?ip=<known device ip>: relay the module's /api/status JSON
    else if (method == "GET" && routePath == "/api/status") {
        IPAddress peerIp;
        int ipPos = query.indexOf("ip=");
        String ipStr = (ipPos >= 0) ? query.substring(ipPos + 3) : String("");
        int ampPos = ipStr.indexOf('&');
        if (ampPos >= 0) ipStr = ipStr.substring(0, ampPos);

        if (!peerIp.fromString(ipStr) || !isKnownPeer(peerIp)) {
            sendResponse(client, "404 Not Found", "application/json", "{\"error\":\"Unknown device\"}");
        } else {
            String body;
            int code = fetchPeerStatus(peerIp, body);
            if (code == 200) {
                sendResponse(client, "200 OK", "application/json", body);
            } else {
                sendResponse(client, "502 Bad Gateway", "application/json",
                             "{\"error\":\"Device request failed\",\"code\":" + String(code) + "}");
            }
        }
    }
    // 404
    else {
        sendResponse(client, "404 Not Found", "text/plain", "404 Not Found");
    }

    log_i("[WEB] Client disconnected");
}

String WebManager::readRequestBody(WiFiClient &client, const String &request, int maxSize) {
    int contentLength = -1;
    String lowerRequest = request;
    lowerRequest.toLowerCase();
    int clPos = lowerRequest.indexOf("content-length:");
    if (clPos >= 0) {
        contentLength = lowerRequest.substring(clPos + 15).toInt();
    }
    if (contentLength < 0 || contentLength > maxSize) {
        // Unknown or oversized length: read at most maxSize bytes (until timeout/disconnect)
        contentLength = maxSize;
    }

    String body = "";
    unsigned long bodyTimeout = millis() + 1000;
    while ((int)body.length() < contentLength && millis() < bodyTimeout) {
        if (client.available()) {
            body += (char)client.read();
        } else if (!client.connected()) {
            break;
        } else {
            delay(1);
        }
    }
    return body;
}

void WebManager::sendResponse(WiFiClient &client, const String &status, const String &contentType, const String &body) {
    String header = "HTTP/1.1 " + status + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + String(body.length()) + "\r\n";
    header += "Connection: close\r\n\r\n";
    client.print(header);
    client.print(body);

    // Let the TCP stack transmit, discard any unread request data (avoids a RST), then close
    delay(100);
    while (client.available()) {
        client.read();
    }
    client.stop();
}

bool WebManager::parseHttpRequest(const String &request, String &method, String &path, String &query) {
    int firstSpace = request.indexOf(' ');
    int secondSpace = request.indexOf(' ', firstSpace + 1);
    int questionMark = request.indexOf('?');
    
    if (firstSpace <= 0 || secondSpace <= firstSpace) {
        return false;
    }

    method = request.substring(0, firstSpace);
    method.toUpperCase();

    if (questionMark > 0 && questionMark < secondSpace) {
        path = request.substring(firstSpace + 1, questionMark);
        query = request.substring(questionMark + 1, secondSpace);
    } else {
        path = request.substring(firstSpace + 1, secondSpace);
        query = "";
    }

    return true;
}

// Mask a secret for display: first 4 characters followed by an asterisk
static String maskToken(const String &token) {
    if (token.length() == 0) {
        return "";
    }
    return token.substring(0, 4) + "*";
}

String WebManager::handleFormSubmit(const String &postData) {
    log_i("[WEB] Form submission received");

    int successCount = 0;
    int errorCount = 0;
    String errors = "";

    // Simple URL-encoded form parser
    int pos = 0;
    while (pos < (int)postData.length()) {
        // Find next parameter
        int eqPos = postData.indexOf('=', pos);
        if (eqPos < 0) break;

        int ampPos = postData.indexOf('&', eqPos);
        if (ampPos < 0) ampPos = postData.length();

        String paramName = postData.substring(pos, eqPos);
        String paramValue = postData.substring(eqPos + 1, ampPos);

        // URL decode (simple: just handle + and %XX)
        paramValue.replace("+", " ");
        
        log_i("[WEB] Processing: %s = %s", paramName.c_str(), paramValue.c_str());

        // Parse parameter name
        ConfigManager::Param cfgParam;
        if (!ConfigManager::parseParamName(paramName, cfgParam)) {
            errorCount++;
            errors += "Unknown: " + paramName + "; ";
            pos = ampPos + 1;
            continue;
        }

        // Set parameter based on type
        bool success = false;
        switch (cfgParam) {
            case ConfigManager::Param::TEMP_HIGH:
            case ConfigManager::Param::TEMP_LOW:
            case ConfigManager::Param::TEMP_OFFSET:
            case ConfigManager::Param::HUMIDITY_HIGH:
            case ConfigManager::Param::HUMIDITY_LOW:
            case ConfigManager::Param::HUMIDITY_OFFSET: {
                float val = paramValue.toFloat();
                _configManager.setFloat(cfgParam, val);
                success = true;
                break;
            }

            case ConfigManager::Param::BAT_ADC_THRESHOLD:
            case ConfigManager::Param::BAT_ADC_NEAR_EMPTY:
            case ConfigManager::Param::POWER_ADC_THRESHOLD:
            case ConfigManager::Param::ALERT_RESEND_DELAY_MINS:
            case ConfigManager::Param::NTP_RESYNC_HOURS:
            case ConfigManager::Param::DST_OFFSET:
            case ConfigManager::Param::SMS_SEND_DISABLED: {
                int val = paramValue.toInt();
                _configManager.setInt(cfgParam, val);
                success = true;
                break;
            }

            case ConfigManager::Param::WIFI_SSID: {
                _configManager.setStringParam(cfgParam, paramValue);
                success = true;
                break;
            }

            case ConfigManager::Param::WIFI_PASSWORD: {
                // Only update password if non-empty (skip if left blank)
                if (paramValue.length() > 0) {
                    _configManager.setStringParam(cfgParam, paramValue);
                    success = true;
                } else {
                    // Empty password field - skip update and don't count as error
                    log_i("[WEB] Password field empty - skipping update");
                    pos = ampPos + 1;
                    continue;
                }
                break;
            }

            case ConfigManager::Param::PUSHOVER_USER_TOKEN:
            case ConfigManager::Param::PUSHOVER_API_TOKEN: {
                // The form only shows a masked token; ignore it if it comes back unchanged
                if (paramValue == maskToken(_configManager.getStringParam(cfgParam))) {
                    log_i("[WEB] %s unchanged - skipping update", paramName.c_str());
                    pos = ampPos + 1;
                    continue;
                }
                _configManager.setStringParam(cfgParam, paramValue);
                success = true;
                break;
            }

            default:
                errorCount++;
                errors += "Unsupported: " + paramName + "; ";
                break;
        }

        if (success) {
            successCount++;
            log_i("[WEB] Successfully updated %s", ConfigManager::getParamName(cfgParam).c_str());
        } else {
            errorCount++;
            errors += "Failed: " + paramName + "; ";
        }

        pos = ampPos + 1;
    }

    // Build JSON response
    String response = "{\"status\":\"ok\",\"updated\":" + String(successCount);
    if (errorCount > 0) {
        response += ",\"errors\":\"" + errors + "\"";
    }
    response += "}";

    log_i("[WEB] Form processed: %d updated, %d errors", successCount, errorCount);
    return response;
}

// Stylesheet shared by the home and configuration pages
static const char PAGE_STYLE[] = R"css(
        body { font-family: Arial, sans-serif; max-width: 600px; margin: 5px auto; padding: 5px; background: #f5f5f5; }
        .container { background: white; padding: 10px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; margin: 0 0 8px 0; font-size: 22px; }
        .datetime { text-align: center; font-size: 12px; color: #444; margin-bottom: 10px; }
        .rev { text-align: center; font-size: 11px; color: #666; margin-bottom: 10px; font-family: monospace; }
        a { color: #007bff; text-decoration: none; }

        .section { margin: 5px 0; padding: 8px; border-left: 4px solid #007bff; background: #f8f9fa; }
        .section-title { font-weight: bold; color: #007bff; margin-bottom: 3px; font-size: 13px; }
        .sections-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin: 10px 0; }
        .sections-grid .section { margin: 0; }
        .sections-full { grid-column: 1 / -1; }
        .sec-alerts { border-left-color: #dc3545; background: #fff5f5; }
        .sec-alerts .section-title { color: #dc3545; }
        .sec-devices { border-left-color: #28a745; background: #f4fbf6; }
        .sec-devices .section-title { color: #28a745; }
        .sec-levels { border-left-color: #17a2b8; background: #f0f7fb; }
        .sec-levels .section-title { color: #17a2b8; }
        .sec-phones { border-left-color: #6f42c1; background: #f8f5fc; }
        .sec-phones .section-title { color: #6f42c1; }
        .empty { padding: 10px; color: #666; }

        .alert-item { margin: 4px 0; padding: 6px; background: white; border-radius: 4px; border-left: 3px solid #ff6b6b; font-size: 12px; }
        .alert-row { display: flex; justify-content: space-between; align-items: flex-start; }
        .alert-body { flex: 1; }
        .alert-code { font-weight: bold; color: #dc3545; font-family: monospace; }
        .alert-title { font-weight: bold; color: #333; display: inline; }
        .alert-cause { color: #666; font-size: 11px; }
        .alert-time { color: #999; font-size: 11px; display: block; margin-top: 2px; }
        .alert-ack { background: #d4edda; color: #155724; padding: 2px 6px; border-radius: 3px; font-weight: bold; font-size: 11px; display: inline-block; margin-left: 4px; }
        .no-alerts { color: #28a745; font-style: italic; font-size: 12px; }

        .level-item { margin: 4px 0; padding: 6px; background: white; border-radius: 4px; border-left: 3px solid #17a2b8; }
        .level-label { font-weight: bold; color: #333; }
        .tag { padding: 2px 6px; border-radius: 3px; font-weight: bold; font-size: 11px; }
        .tag-high { background: #f8d7da; color: #721c24; }
        .tag-low { background: #cfe2ff; color: #084298; }
        .tag-ok { background: #d4edda; color: #155724; }
        .tag-warn { background: #fff3cd; color: #664d03; }
        .tag-admin { background: #d32f2f; color: white; }
        .tag-read { background: #1976d2; color: white; }
        .tag-muted { background: #ff9800; color: white; }

        .phone-list { display: flex; flex-direction: column; gap: 8px; }
        .phone-item { display: flex; justify-content: space-between; align-items: center; padding: 8px; background: white; border-radius: 4px; border-left: 3px solid #1976d2; }
        .phone-item.admin { border-left-color: #d32f2f; }
        .phone-main { flex: 1; }
        .phone-number { font-family: monospace; }
        .phone-alias { font-size: 12px; color: #666; margin-top: 2px; }
        .phone-badges { display: flex; gap: 4px; align-items: center; }

        table.devices { width: 100%; border-collapse: collapse; font-size: 13px; background: white; }
        table.devices th { text-align: left; border-bottom: 1px solid #ddd; }
        .dev-type { color: #666; font-size: 11px; }
        dialog { max-width: 90%; border-radius: 6px; }
        dialog pre { white-space: pre-wrap; word-break: break-all; font-size: 12px; margin: 0 0 8px 0; font-family: monospace; }
        dialog button { margin-top: 0; }

        label { display: block; margin-bottom: 2px; font-weight: bold; color: #333; font-size: 12px; }
        .form-group { margin: 2px 0; }
        input, textarea { width: 100%; padding: 6px; margin-bottom: 3px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-size: 14px; }
        .check-row { display: flex; align-items: center; }
        .check-row input { width: auto; margin: 0 8px 0 0; }
        .check-row label { margin-bottom: 0; font-weight: normal; }
        .hint { font-size: 11px; color: #666; margin-top: 5px; }

        button { width: 100%; padding: 8px; background: #007bff; color: white; border: none; border-radius: 4px; font-size: 14px; cursor: pointer; margin-top: 10px; }
        button:hover { background: #0056b3; }
        a.btn { display: block; text-align: center; padding: 8px; margin-top: 10px; background: #007bff; color: white; border-radius: 4px; font-size: 14px; }
        a.btn:hover { background: #0056b3; }
        a.back { display: block; text-align: center; padding: 6px; margin-bottom: 8px; font-size: 13px; }
        a.btn-sm { display: inline-block; margin: 0; padding: 2px 8px; font-size: 12px; }
        button.btn-sm { width: auto; margin: 0; padding: 2px 8px; font-size: 12px; }
        button.btn-warn { background: #ff9800; }
        button.btn-danger { background: #dc3545; }
        button.ack-btn { margin-left: 8px; padding: 1px 3px; background: #28a745; border-radius: 2px; font-size: 9px; line-height: 1; white-space: nowrap; width: auto; min-width: auto; flex-shrink: 0; }
        .actions { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; }
        .actions .wide { grid-column: 1 / -1; }

        .status { margin-top: 10px; padding: 6px; border-radius: 4px; display: none; font-size: 12px; }
        .status.success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .status.error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
)css";

// Common page start: head, styles, title, date/time and revision
String WebManager::generatePageStart(const String &title, const String &heading) {
    String html = "";
    html += "<!DOCTYPE html>\n";
    html += "<html>\n<head>\n";
    html += "    <title>" + title + "</title>\n";
    html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    html += "    <style>";
    html += PAGE_STYLE;
    html += "    </style>\n";
    html += "</head>\n<body>\n";
    html += "    <div class=\"container\">\n";
    html += "        <h1>" + heading + "</h1>\n";
    
    // Display current date/time from ClockManager
    String dateTime = _clockManager.getFormattedDateTime();
    html += "        <div class=\"datetime\">\n";
    html += "            Device Date/Time: " + dateTime + "\n";
    html += "        </div>\n";
    
    // Display revision and git commit info
#ifdef GIT_REV
    html += "        <div class=\"rev\">\n";
    html += "            Rev: " + String(GIT_REV) + "\n";
    html += "        </div>\n";
#endif

    return html;
}

String WebManager::generateHomePage() {
    String html = generatePageStart("SMS Relay", "SMS Relay");

    // Alerts section
    html += "        <div class=\"section sections-full sec-alerts\">\n";
    html += "            <div class=\"section-title\">Active Alerts</div>\n";
    html += generateAlertsHTML();
    html += "        </div>\n";

    // Known devices (recorded from /api/ping)
    html += "        <div class=\"section sections-full sec-devices\">\n";
    html += "            <div class=\"section-title\">Known Devices</div>\n";
    html += generatePeersHTML();
    html += "        </div>\n";
    
    // Current Levels section
    html += "        <div class=\"section sections-full sec-levels\">\n";
    html += "            <div class=\"section-title\">Current Levels</div>\n";
    html += generateLevelsHTML();
    html += "        </div>\n";
    
    // Configured Phone Numbers section
    html += "        <div class=\"section sections-full sec-phones\">\n";
    html += "            <div class=\"section-title\">Configured Phone Numbers</div>\n";
    std::vector<PhoneNumberManager::AuthorizedNumber> numbers = _phoneNumberManager.getAllNumbers();
    if (numbers.empty()) {
        html += "            <div class=\"empty\">No phone numbers configured</div>\n";
    } else {
        html += "            <div class=\"phone-list\">\n";
        for (const auto& num : numbers) {
            String permission = (num.permission == PhoneNumberManager::Permission::ADMIN) ? "ADMIN" : 
                               (num.permission == PhoneNumberManager::Permission::READ) ? "READ-ONLY" : "NONE";
            bool isAdmin = (num.permission == PhoneNumberManager::Permission::ADMIN);
            html += "                <div class=\"phone-item" + String(isAdmin ? " admin" : "") + "\">\n";
            html += "                    <div class=\"phone-main\">\n";
            html += "                        <div class=\"phone-number\">" + num.number + "</div>\n";
            if (num.alias.length() > 0) {
                html += "                        <div class=\"phone-alias\">" + num.alias + "</div>\n";
            }
            html += "                    </div>\n";
            html += "                    <div class=\"phone-badges\">\n";
            if (num.muted) {
                html += "                        <span class=\"tag tag-muted\">MUTED</span>\n";
            }
            html += "                        <span class=\"tag " + String(isAdmin ? "tag-admin" : "tag-read") + "\">" + permission + "</span>\n";
            html += "                    </div>\n";
            html += "                </div>\n";
        }
        html += "            </div>\n";
    }
    html += "        </div>\n";
    
    html += "        <a class=\"btn\" href=\"/" + _accessToken + "/config\">Configuration</a>\n";
    html += "        <div id=\"status\" class=\"status\"></div>\n";
    html += "    </div>\n";

    html += "    <script>\n";
    html += "        const ACCESS_TOKEN = '" + _accessToken + "';\n";
    html += "        // Handle ACK buttons for alerts\n";
    html += "        document.querySelectorAll('.ack-btn').forEach(btn => {\n";
    html += "            btn.addEventListener('click', async function(e) {\n";
    html += "                e.preventDefault();\n";
    html += "                const code = this.getAttribute('data-code');\n";
    html += "                const params = new URLSearchParams();\n";
    html += "                params.append('code', code);\n";
    html += "                try {\n";
    html += "                    const response = await fetch('/' + ACCESS_TOKEN + '/api/ack', {\n";
    html += "                        method: 'POST',\n";
    html += "                        body: params.toString(),\n";
    html += "                        headers: {'Content-Type': 'application/x-www-form-urlencoded'}\n";
    html += "                    });\n";
    html += "                    const text = await response.text();\n";
    html += "                    const result = JSON.parse(text);\n";
    html += "                    const statusDiv = document.getElementById('status');\n";
    html += "                    if (result.status === 'ok') {\n";
    html += "                        this.style.display = 'none';\n";
    html += "                        statusDiv.className = 'status success';\n";
    html += "                        statusDiv.textContent = 'Alert [' + code + '] acknowledged';\n";
    html += "                        statusDiv.style.display = 'block';\n";
    html += "                        setTimeout(() => location.reload(), 1500);\n";
    html += "                    } else {\n";
    html += "                        statusDiv.className = 'status error';\n";
    html += "                        statusDiv.textContent = 'Error: ' + result.message;\n";
    html += "                        statusDiv.style.display = 'block';\n";
    html += "                    }\n";
    html += "                } catch (error) {\n";
    html += "                    const statusDiv = document.getElementById('status');\n";
    html += "                    statusDiv.className = 'status error';\n";
    html += "                    statusDiv.textContent = 'Network error: ' + error.message;\n";
    html += "                    statusDiv.style.display = 'block';\n";
    html += "                }\n";
    html += "            });\n";
    html += "        });\n";
    html += "        \n";
    html += "        // Known devices: show the /api/status JSON in a dialog\n";
    html += "        const statusDialog = document.getElementById('statusDialog');\n";
    html += "        if (statusDialog) {\n";
    html += "            document.getElementById('statusQuit').addEventListener('click', () => statusDialog.close());\n";
    html += "            document.querySelectorAll('.status-btn').forEach(btn => {\n";
    html += "                btn.addEventListener('click', async function() {\n";
    html += "                    const ip = this.getAttribute('data-ip');\n";
    html += "                    const body = document.getElementById('statusBody');\n";
    html += "                    body.textContent = 'Loading ' + ip + ' ...';\n";
    html += "                    statusDialog.showModal();\n";
    html += "                    try {\n";
    html += "                        const response = await fetch('/' + ACCESS_TOKEN + '/api/status?ip=' + encodeURIComponent(ip));\n";
    html += "                        const text = await response.text();\n";
    html += "                        try {\n";
    html += "                            body.textContent = JSON.stringify(JSON.parse(text), null, 2);\n";
    html += "                        } catch (parseError) {\n";
    html += "                            body.textContent = text;\n";
    html += "                        }\n";
    html += "                    } catch (error) {\n";
    html += "                        body.textContent = 'Network error: ' + error.message;\n";
    html += "                    }\n";
    html += "                });\n";
    html += "            });\n";
    html += "        }\n";
    html += "    </script>\n";
    html += "</body>\n</html>\n";

    return html;
}

String WebManager::generateConfigPage() {
    // Build HTML with current parameter values from ConfigManager
    String html = generatePageStart("SMS Relay Configuration", "Configuration");

    html += "        <a class=\"back\" href=\"/" + _accessToken + "/\">&larr; Back to status</a>\n";
    html += "        <form id=\"configForm\" data-token=\"" + _accessToken + "\">\n";
    html += "        <script>const ACCESS_TOKEN = '" + _accessToken + "';</script>\n";
    
    html += "            <div class=\"sections-grid\">\n";

    // Temperature section
    html += "                <div class=\"section\">\n";
    html += "                    <div class=\"section-title\">Temperature (°C)</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>High Alert: <input type=\"number\" step=\"0.1\" name=\"TEMP_HIGH\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::TEMP_HIGH), 1) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Low Alert: <input type=\"number\" step=\"0.1\" name=\"TEMP_LOW\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::TEMP_LOW), 1) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Offset: <input type=\"number\" step=\"0.1\" name=\"TEMP_OFFSET\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::TEMP_OFFSET), 1) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // Humidity section
    html += "                <div class=\"section\">\n";
    html += "                    <div class=\"section-title\">Humidity (%)</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>High Alert: <input type=\"number\" step=\"0.1\" name=\"HUMIDITY_HIGH\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::HUMIDITY_HIGH), 1) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Low Alert: <input type=\"number\" step=\"0.1\" name=\"HUMIDITY_LOW\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::HUMIDITY_LOW), 1) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Offset: <input type=\"number\" step=\"0.1\" name=\"HUMIDITY_OFFSET\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::HUMIDITY_OFFSET), 1) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // Battery section
    html += "                <div class=\"section\">\n";
    html += "                    <div class=\"section-title\">Battery (ADC)</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Threshold: <input type=\"number\" name=\"BAT_THRESHOLD\" value=\"" + String(_configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD)) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Near Empty: <input type=\"number\" name=\"BAT_NEAR_EMPTY\" value=\"" + String(_configManager.getInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY)) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // Power section
    html += "                <div class=\"section\">\n";
    html += "                    <div class=\"section-title\">Power (ADC)</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Threshold: <input type=\"number\" name=\"POWER_THRESHOLD\" value=\"" + String(_configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD)) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // Alerts settings section
    html += "                <div class=\"section\">\n";
    html += "                    <div class=\"section-title\">Alerts & SMS</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Resend Delay (minutes): <input type=\"number\" name=\"RESEND_MINS\" value=\"" + String(_configManager.getInt(ConfigManager::Param::ALERT_RESEND_DELAY_MINS)) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group check-row\">\n";
    html += "                        <input type=\"checkbox\" id=\"SMS_DISABLED\" name=\"SMS_DISABLED\" value=\"1\" " + (String(_configManager.getInt(ConfigManager::Param::SMS_SEND_DISABLED)) == "1" ? String("checked") : String("")) + ">\n";
    html += "                        <label for=\"SMS_DISABLED\">Disable SMS notifications</label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // Network section
    html += "                <div class=\"section\">\n";
    html += "                    <div class=\"section-title\">Network</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>NTP Resync (hours): <input type=\"number\" name=\"NTP_RESYNC_HRS\" value=\"" + String(_configManager.getInt(ConfigManager::Param::NTP_RESYNC_HOURS)) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>DST Offset (seconds): <input type=\"number\" name=\"DST_OFFSET\" value=\"" + String(_configManager.getInt(ConfigManager::Param::DST_OFFSET)) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // WiFi section (full width)
    html += "                <div class=\"section sections-full\">\n";
    html += "                    <div class=\"section-title\">WiFi Credentials</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>SSID: <input type=\"text\" name=\"WIFI_SSID\" value=\"" + _configManager.getStringParam(ConfigManager::Param::WIFI_SSID) + "\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>Password: <input type=\"password\" name=\"WIFI_PWD\" placeholder=\"Leave empty to keep current\"></label>\n";
    html += "                    </div>\n";
    html += "                </div>\n";

    // Pushover Notifier section (full width)
    html += "                <div class=\"section sections-full\">\n";
    html += "                    <div class=\"section-title\">Pushover Notifications</div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>User Token: <input type=\"text\" name=\"PUSHOVER_USER\" class=\"secret\" autocomplete=\"off\" value=\"" + maskToken(_configManager.getStringParam(ConfigManager::Param::PUSHOVER_USER_TOKEN)) + "\" data-original=\"" + maskToken(_configManager.getStringParam(ConfigManager::Param::PUSHOVER_USER_TOKEN)) + "\" placeholder=\"Leave empty to disable\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"form-group\">\n";
    html += "                        <label>API Token: <input type=\"text\" name=\"PUSHOVER_TOKEN\" class=\"secret\" autocomplete=\"off\" value=\"" + maskToken(_configManager.getStringParam(ConfigManager::Param::PUSHOVER_API_TOKEN)) + "\" data-original=\"" + maskToken(_configManager.getStringParam(ConfigManager::Param::PUSHOVER_API_TOKEN)) + "\" placeholder=\"Leave empty to disable\"></label>\n";
    html += "                    </div>\n";
    html += "                    <div class=\"hint\">\n";
    html += "                        Get tokens from <a href=\"https://pushover.net\" target=\"_blank\">pushover.net</a>\n";
    html += "                    </div>\n";
    html += "                </div>\n";
    
    html += "            </div>\n";

    html += "            <div class=\"actions\">\n";
    html += "                <button type=\"submit\">Save</button>\n";
    html += "                <button type=\"button\" id=\"restartBtn\" class=\"btn-warn\">Restart</button>\n";
    html += "                <button type=\"button\" id=\"weboffBtn\" class=\"btn-danger wide\">Web Off</button>\n";
    html += "            </div>\n";
    html += "        </form>\n";
    html += "        <div id=\"status\" class=\"status\"></div>\n";
    html += "    </div>\n";

    // JavaScript for form submission
    html += "    <script>\n";
    html += "        document.getElementById('configForm').addEventListener('submit', async function(e) {\n";
    html += "            e.preventDefault();\n";
    html += "            const formData = new FormData(this);\n";
    html += "            const params = new URLSearchParams();\n";
    html += "            for (let [key, value] of formData) {\n";
    html += "                const el = this.elements[key];\n";
    html += "                if (el && el.classList && el.classList.contains('secret')) {\n";
    html += "                    // Secret fields: only send when modified (empty allowed to clear)\n";
    html += "                    if (value !== el.dataset.original) params.append(key, value);\n";
    html += "                } else if (value) {\n";
    html += "                    params.append(key, value);\n";
    html += "                }\n";
    html += "            }\n";
    html += "            // Handle SMS_DISABLED checkbox: send 0 if unchecked, 1 if checked\n";
    html += "            const smsCheckbox = document.getElementById('SMS_DISABLED');\n";
    html += "            if (smsCheckbox) {\n";
    html += "                params.set('SMS_DISABLED', smsCheckbox.checked ? '1' : '0');\n";
    html += "            }\n";
    html += "            try {\n";
    html += "                const response = await fetch('/' + ACCESS_TOKEN + '/api/config', {\n";
    html += "                    method: 'POST',\n";
    html += "                    body: params.toString(),\n";
    html += "                    headers: {'Content-Type': 'application/x-www-form-urlencoded'}\n";
    html += "                });\n";
    html += "                const text = await response.text();\n";
    html += "                const result = JSON.parse(text);\n";
    html += "                const statusDiv = document.getElementById('status');\n";
    html += "                if (result.status === 'ok') {\n";
    html += "                    statusDiv.className = 'status success';\n";
    html += "                    statusDiv.textContent = 'Configuration saved! (' + result.updated + ' parameters updated)';\n";
    html += "                } else {\n";
    html += "                    statusDiv.className = 'status error';\n";
    html += "                    statusDiv.textContent = 'Error: ' + (result.errors || 'Unknown error');\n";
    html += "                }\n";
    html += "                statusDiv.style.display = 'block';\n";
    html += "                setTimeout(() => statusDiv.style.display = 'none', 5000);\n";
    html += "            } catch (error) {\n";
    html += "                const statusDiv = document.getElementById('status');\n";
    html += "                statusDiv.className = 'status error';\n";
    html += "                statusDiv.textContent = 'Network error: ' + error.message;\n";
    html += "                statusDiv.style.display = 'block';\n";
    html += "            }\n";
    html += "        });\n";
    html += "        \n";
    html += "        document.getElementById('restartBtn').addEventListener('click', async function() {\n";
    html += "            if (!confirm('Restart the device?')) return;\n";
    html += "            try {\n";
    html += "                const response = await fetch('/' + ACCESS_TOKEN + '/api/restart', { method: 'POST' });\n";
    html += "                const text = await response.text();\n";
    html += "                const result = JSON.parse(text);\n";
    html += "                const statusDiv = document.getElementById('status');\n";
    html += "                if (result.status === 'ok') {\n";
    html += "                    statusDiv.className = 'status success';\n";
    html += "                    statusDiv.textContent = 'Device restarting...';\n";
    html += "                    statusDiv.style.display = 'block';\n";
    html += "                    document.getElementById('configForm').style.display = 'none';\n";
    html += "                } else {\n";
    html += "                    statusDiv.className = 'status error';\n";
    html += "                    statusDiv.textContent = 'Restart failed: ' + (result.error || 'Unknown error');\n";
    html += "                    statusDiv.style.display = 'block';\n";
    html += "                }\n";
    html += "            } catch (error) {\n";
    html += "                const statusDiv = document.getElementById('status');\n";
    html += "                statusDiv.className = 'status error';\n";
    html += "                statusDiv.textContent = 'Network error: ' + error.message;\n";
    html += "                statusDiv.style.display = 'block';\n";
    html += "            }\n";
    html += "        });\n";
    html += "        \n";
    html += "        document.getElementById('weboffBtn').addEventListener('click', async function() {\n";
    html += "            if (!confirm('Stop web server?')) return;\n";
    html += "            try {\n";
    html += "                const response = await fetch('/' + ACCESS_TOKEN + '/api/weboff', { method: 'POST' });\n";
    html += "                const text = await response.text();\n";
    html += "                const result = JSON.parse(text);\n";
    html += "                const statusDiv = document.getElementById('status');\n";
    html += "                if (result.status === 'ok') {\n";
    html += "                    statusDiv.className = 'status success';\n";
    html += "                    statusDiv.textContent = 'Web server stopped.';\n";
    html += "                    statusDiv.style.display = 'block';\n";
    html += "                    document.getElementById('configForm').style.display = 'none';\n";
    html += "                } else {\n";
    html += "                    statusDiv.className = 'status error';\n";
    html += "                    statusDiv.textContent = 'Failed to stop web server: ' + (result.error || 'Unknown error');\n";
    html += "                    statusDiv.style.display = 'block';\n";
    html += "                }\n";
    html += "            } catch (error) {\n";
    html += "                const statusDiv = document.getElementById('status');\n";
    html += "                statusDiv.className = 'status error';\n";
    html += "                statusDiv.textContent = 'Network error: ' + error.message;\n";
    html += "                statusDiv.style.display = 'block';\n";
    html += "            }\n";
    html += "        });\n";
    html += "    </script>\n";
    html += "</body>\n</html>\n";

    return html;
}

String WebManager::generateRandomToken() {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    String token = "";
    
    for (int i = 0; i < 10; i++) {
        int index = random(0, sizeof(charset) - 1);
        token += charset[index];
    }
    
    return token;
}

String WebManager::generateAlertsHTML() {
    String alertsContent = _alertManager.getPendingAlertsList();
    String html = "";
    
    // Check if there are no alerts
    if (alertsContent.indexOf("No pending alerts") >= 0) {
        html += "            <div class=\"no-alerts\">✓ No active alerts</div>\n";
        return html;
    }
    
    // Parse the alerts list and format as HTML
    // The format is: "[CODE] TITLE (CAUSE) @ TIME [ACK:N]"
    int startPos = alertsContent.indexOf("Pending alerts:");
    if (startPos < 0) {
        html += "            <div class=\"no-alerts\">Unable to load alerts</div>\n";
        return html;
    }
    
    startPos += 15;  // Length of "Pending alerts:"
    
    // Process each line
    int currentPos = startPos;
    while (currentPos < (int)alertsContent.length()) {
        // Find the end of the line
        int lineEnd = alertsContent.indexOf('\n', currentPos);
        if (lineEnd < 0) lineEnd = alertsContent.length();
        
        String line = alertsContent.substring(currentPos, lineEnd);
        line.trim();
        
        if (line.length() > 0 && line[0] == '[') {
            // Parse alert line
            int codeEnd = line.indexOf(']');
            String code = line.substring(1, codeEnd);
            String remaining = line.substring(codeEnd + 2);  // Skip "] "
            
            // Extract ACK info
            String ackInfo = "";
            int ackPos = remaining.indexOf("[ACK:");
            String title = remaining;
            if (ackPos >= 0) {
                title = remaining.substring(0, ackPos);
                int ackEnd = remaining.indexOf(']', ackPos);
                ackInfo = remaining.substring(ackPos, ackEnd + 1);
            }
            
            // Extract cause and timestamp
            String cause = "";
            String timeStr = "";
            int causeStart = title.indexOf(" (");
            int causeEnd = title.indexOf(")");
            if (causeStart >= 0 && causeEnd > causeStart) {
                cause = title.substring(causeStart + 2, causeEnd);
                title = title.substring(0, causeStart);
                
                // Extract timestamp after cause
                int timeStart = remaining.indexOf("@ ");
                if (timeStart >= 0 && ackPos > timeStart) {
                    timeStr = remaining.substring(timeStart + 2, ackPos >= 0 ? ackPos : remaining.length());
                }
            } else {
                // No cause, check for timestamp
                int timeStart = remaining.indexOf("@ ");
                if (timeStart >= 0) {
                    if (ackPos >= 0) {
                        timeStr = remaining.substring(timeStart + 2, ackPos);
                    } else {
                        timeStr = remaining.substring(timeStart + 2);
                    }
                }
            }
            
            timeStr.trim();
            
            // Format as HTML alert item
            html += "            <div class=\"alert-item\">\n";
            html += "                <div class=\"alert-row\">\n";
            html += "                    <div class=\"alert-body\">\n";
            html += "                        <span class=\"alert-code\">[" + code + "]</span> ";
            html += "<span class=\"alert-title\">" + title + "</span>";
            if (ackInfo.length() > 0) {
                html += " <span class=\"alert-ack\">" + ackInfo + "</span>";
            }
            html += "\n";
            if (cause.length() > 0) {
                html += "                        <div class=\"alert-cause\">Cause: " + cause + "</div>\n";
            }
            if (timeStr.length() > 0) {
                html += "                        <div class=\"alert-time\">Time: " + timeStr + "</div>\n";
            }
            html += "                    </div>\n";
            if (ackInfo.length() == 0) {
                html += "                    <button type=\"button\" class=\"ack-btn\" data-code=\"" + code + "\">ACK</button>\n";
            }
            html += "                </div>\n";
            html += "            </div>\n";
        }
        
        currentPos = lineEnd + 1;
    }
    
    return html;
}

String WebManager::generateLevelsHTML() {
    String html = "";
    
    // Temperature status
    float temp = _tempHumidityProcessor.getTemperature();
    float tempHigh = _configManager.getFloat(ConfigManager::Param::TEMP_HIGH);
    float tempLow = _configManager.getFloat(ConfigManager::Param::TEMP_LOW);
    
    html += "            <div class=\"level-item\">\n";
    html += "                <span class=\"level-label\">Temperature:</span> " + String(temp, 1) + "°C\n";
    if (temp > tempHigh) {
        html += " <span class=\"tag tag-high\">[HIGH]</span>\n";
    } else if (temp < tempLow) {
        html += " <span class=\"tag tag-low\">[LOW]</span>\n";
    } else {
        html += " <span class=\"tag tag-ok\">[OK]</span>\n";
    }
    html += "            </div>\n";
    
    // Humidity status
    float humidity = _tempHumidityProcessor.getHumidity();
    float humidityHigh = _configManager.getFloat(ConfigManager::Param::HUMIDITY_HIGH);
    float humidityLow = _configManager.getFloat(ConfigManager::Param::HUMIDITY_LOW);
    
    html += "            <div class=\"level-item\">\n";
    html += "                <span class=\"level-label\">Humidity:</span> " + String(humidity, 1) + "%\n";
    if (humidity > humidityHigh) {
        html += " <span class=\"tag tag-high\">[HIGH]</span>\n";
    } else if (humidity < humidityLow) {
        html += " <span class=\"tag tag-low\">[LOW]</span>\n";
    } else {
        html += " <span class=\"tag tag-ok\">[OK]</span>\n";
    }
    html += "            </div>\n";
    
    // Battery status
#ifdef BOARD_BAT_ADC_PIN
    int batAdcValue = BatteryProcessor::readBatADC();
    int batAdcThreshold = _configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD);
    int batAdcNearEmpty = _configManager.getInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY);
    
    html += "            <div class=\"level-item\">\n";
    html += "                <span class=\"level-label\">Battery:</span> " + String(batAdcValue) + "\n";
    if (batAdcValue < batAdcNearEmpty) {
        html += " <span class=\"tag tag-high\">[NEAR EMPTY]</span>\n";
    } else if (batAdcValue < batAdcThreshold) {
        html += " <span class=\"tag tag-warn\">[USB]</span>\n";
    } else {
        html += " <span class=\"tag tag-ok\">[OK]</span>\n";
    }
    html += "            </div>\n";
#endif
    
    // Main power status
    int mainAdcValue = MainPowerCheck::readGPIO00ADC();
    int powerAdcThreshold = _configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD);
    
    html += "            <div class=\"level-item\">\n";
    html += "                <span class=\"level-label\">Main Power:</span> " + String(mainAdcValue) + "\n";
    if (mainAdcValue < powerAdcThreshold) {
        html += " <span class=\"tag tag-high\">[LOW]</span>\n";
    } else {
        html += " <span class=\"tag tag-ok\">[OK]</span>\n";
    }
    html += "            </div>\n";
    
    return html;
}

