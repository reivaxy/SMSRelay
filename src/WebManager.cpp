#include "WebManager.h"
#include "ConfigManager.h"
#include "SMSSender.h"
#include "PhoneNumberManager.h"
#include <ESPmDNS.h>
#include <ctime>

WebManager::WebManager(ConfigManager &configManager, SMSSender &sender, PhoneNumberManager &phoneNumberManager)
    : _server(nullptr), _configManager(configManager), _sender(sender), _phoneNumberManager(phoneNumberManager),
      _isRunning(false), _requesterNumber(""), _serverURL(""), _accessToken("") {}

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
        _sender.send(requesterNumber, "ERROR: WiFi SSID not configured. Use: CONFIG WIFI_SSID <ssid>");
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
        _sender.send(requesterNumber, "ERROR: Failed to connect to WiFi. Check SSID and password.");
        return false;
    }

    log_i("[WEB] WiFi connected. IP: %s", WiFi.localIP().toString().c_str());

    // Initialize mDNS to advertise as smsrelay.local
    if (!MDNS.begin("smsrelay")) {
        log_e("[WEB] Failed to start mDNS");
        WiFi.disconnect(true);
        return false;
    }
    log_i("[WEB] mDNS started as smsrelay.local");

    // Generate random access token
    _accessToken = generateRandomToken();
    log_i("[WEB] Generated access token: %s", _accessToken.c_str());

    // Create web server on port 80
    _server = new WiFiServer(80);
    _server->begin();
    _isRunning = true;

    // Add mDNS service for HTTP
    MDNS.addService("http", "tcp", 80);

    // Build server URLs with access token
    String ipURL = "http://" + WiFi.localIP().toString() + "/" + _accessToken + "/";
    _serverURL = "http://smsrelay.local/" + _accessToken + "/";
    log_i("[WEB] Web server started at: %s (also accessible via IP: %s)", _serverURL.c_str(), ipURL.c_str());

    // Send both URLs to requester
    _sender.send(requesterNumber, "OK: Web interface started\n" + _serverURL + "\nor\n" + ipURL);

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

    // Stop mDNS
    MDNS.end();

    WiFi.disconnect(true);  // true = turn off WiFi radio
    _isRunning = false;
    log_i("[WEB] Web server stopped and WiFi disconnected");
}

bool WebManager::isRunning() const {
    return _isRunning;
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
    
    while (client.connected() && millis() < timeout) {
        if (client.available()) {
            char c = client.read();
            request += c;
            if (request.endsWith("\r\n\r\n")) {
                break;
            }
        }
    }

    if (request.length() == 0) {
        client.stop();
        return;
    }

    // Parse request
    String method, path, query;
    if (!parseHttpRequest(request, method, path, query)) {
        client.stop();
        return;
    }

    log_i("[WEB] %s %s", method.c_str(), path.c_str());

    // Check if path contains the access token
    if (!path.startsWith("/" + _accessToken)) {
        log_w("[WEB] Access denied - invalid token in path: %s", path.c_str());
        String notFound = "403 Forbidden";
        client.println("HTTP/1.1 403 Forbidden");
        client.println("Content-Type: text/plain");
        client.println("Content-Length: " + String(notFound.length()));
        client.println("Connection: close");
        client.println();
        client.println(notFound);
        delay(100);
        client.stop();
        return;
    }

    // Remove token prefix from path for routing
    String routePath = path.substring(("/" + _accessToken).length());
    if (routePath.length() == 0 || routePath == "/") {
        routePath = "/";
    }

    // Handle GET /
    if (method == "GET" && (routePath == "/" || routePath == "")) {
        String html = generateHTMLForm();
        
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=UTF-8");
        client.println("Content-Length: " + String(html.length()));
        client.println("Connection: close");
        client.println();
        client.println(html);
    }
    // Handle POST /api/config
    else if (method == "POST" && routePath == "/api/config") {
        // Read POST body
        String postBody = "";
        unsigned long bodyTimeout = millis() + 1000;
        
        while (client.connected() && millis() < bodyTimeout) {
            if (client.available()) {
                postBody += (char)client.read();
            }
        }

        String response = handleFormSubmit(postBody);
        
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Content-Length: " + String(response.length()));
        client.println("Connection: close");
        client.println();
        client.println(response);
    }
    // Handle POST /api/restart
    else if (method == "POST" && routePath == "/api/restart") {
        log_i("[WEB] Restart request received");
        String response = "{\"status\":\"ok\",\"message\":\"Device restarting\"}";
        
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Content-Length: " + String(response.length()));
        client.println("Connection: close");
        client.println();
        client.println(response);
        
        delay(100);
        client.stop();
        
        // Restart device after sending response
        delay(500);
        ESP.restart();
        return;
    }
    // Handle POST /api/weboff
    else if (method == "POST" && routePath == "/api/weboff") {
        log_i("[WEB] Web off request received");
        String response = "{\"status\":\"ok\",\"message\":\"Web server stopping\"}";
        
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Content-Length: " + String(response.length()));
        client.println("Connection: close");
        client.println();
        client.println(response);
        
        delay(100);
        client.stop();
        
        // Stop web server after sending response
        stop();
        return;
    }
    // 404
    else {
        String notFound = "404 Not Found";
        client.println("HTTP/1.1 404 Not Found");
        client.println("Content-Type: text/plain");
        client.println("Content-Length: " + String(notFound.length()));
        client.println("Connection: close");
        client.println();
        client.println(notFound);
    }

    delay(100);
    client.stop();
    log_i("[WEB] Client disconnected");
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

String WebManager::generateHTMLForm() {
    // Build HTML with current parameter values from ConfigManager
    String html = "";
    html += "<!DOCTYPE html>\n";
    html += "<html>\n<head>\n";
    html += "    <title>SMS Relay Configuration</title>\n";
    html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    html += "    <style>\n";
    html += "        body { font-family: Arial, sans-serif; max-width: 600px; margin: 5px auto; padding: 5px; background: #f5f5f5; }\n";
    html += "        .container { background: white; padding: 10px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n";
    html += "        h1 { color: #333; text-align: center; margin: 0 0 8px 0; font-size: 22px; }\n";
    html += "        .section { margin: 5px 0; padding: 8px; border-left: 4px solid #007bff; background: #f8f9fa; }\n";
    html += "        .section-title { font-weight: bold; color: #007bff; margin-bottom: 3px; font-size: 13px; }\n";
    html += "        .form-group { margin: 2px 0; }\n";
    html += "        label { display: block; margin-bottom: 2px; font-weight: bold; color: #333; font-size: 12px; }\n";
    html += "        input, textarea { width: 100%; padding: 6px; margin-bottom: 3px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-size: 14px; }\n";
    html += "        input[type=\"number\"] { width: 100%; }\n";
    html += "        button { width: 100%; padding: 8px; background: #007bff; color: white; border: none; border-radius: 4px; font-size: 14px; cursor: pointer; margin-top: 10px; }\n";
    html += "        button:hover { background: #0056b3; }\n";
    html += "        .status { margin-top: 10px; padding: 6px; border-radius: 4px; display: none; font-size: 12px; }\n";
    html += "        .status.success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }\n";
    html += "        .status.error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }\n";
    html += "        h1 { margin-bottom: 8px; }\n";
    html += "    </style>\n";
    html += "</head>\n<body>\n";
    html += "    <div class=\"container\">\n";
    html += "        <h1>Configuration</h1>\n";
    
    // Display current date/time with DST offset
    time_t now = time(nullptr);
    int dstOffset = _configManager.getInt(ConfigManager::Param::DST_OFFSET);
    time_t adjustedTime = now + dstOffset;
    struct tm* timeinfo = localtime(&adjustedTime);
    char dateBuffer[64];
    strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    html += "        <div style=\"text-align: center; font-size: 12px; color: #666; margin-bottom: 10px;\">\n";
    html += "            Device Date/Time: " + String(dateBuffer) + "\n";
    html += "        </div>\n";
    html += "        <form id=\"configForm\" data-token=\"" + _accessToken + "\">\n";
    html += "        <script>const ACCESS_TOKEN = '" + _accessToken + "';</script>\n";

    // Temperature section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">Temperature (°C)</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>High Alert: <input type=\"number\" step=\"0.1\" name=\"TEMP_HIGH\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::TEMP_HIGH), 1) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Low Alert: <input type=\"number\" step=\"0.1\" name=\"TEMP_LOW\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::TEMP_LOW), 1) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Offset: <input type=\"number\" step=\"0.1\" name=\"TEMP_OFFSET\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::TEMP_OFFSET), 1) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // Humidity section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">Humidity (%)</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>High Alert: <input type=\"number\" step=\"0.1\" name=\"HUMIDITY_HIGH\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::HUMIDITY_HIGH), 1) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Low Alert: <input type=\"number\" step=\"0.1\" name=\"HUMIDITY_LOW\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::HUMIDITY_LOW), 1) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Offset: <input type=\"number\" step=\"0.1\" name=\"HUMIDITY_OFFSET\" value=\"" + String(_configManager.getFloat(ConfigManager::Param::HUMIDITY_OFFSET), 1) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // Battery section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">Battery (ADC)</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Threshold: <input type=\"number\" name=\"BAT_THRESHOLD\" value=\"" + String(_configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD)) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Near Empty: <input type=\"number\" name=\"BAT_NEAR_EMPTY\" value=\"" + String(_configManager.getInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY)) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // Power section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">Power (ADC)</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Threshold: <input type=\"number\" name=\"POWER_THRESHOLD\" value=\"" + String(_configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD)) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // Alerts section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">Alerts</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Resend Delay (minutes): <input type=\"number\" name=\"RESEND_MINS\" value=\"" + String(_configManager.getInt(ConfigManager::Param::ALERT_RESEND_DELAY_MINS)) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // Network section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">Network</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>NTP Resync (hours): <input type=\"number\" name=\"NTP_RESYNC_HRS\" value=\"" + String(_configManager.getInt(ConfigManager::Param::NTP_RESYNC_HOURS)) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>DST Offset (seconds): <input type=\"number\" name=\"DST_OFFSET\" value=\"" + String(_configManager.getInt(ConfigManager::Param::DST_OFFSET)) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // SMS section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">SMS</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Send Disabled (0=enabled, 1=disabled): <input type=\"number\" name=\"SMS_DISABLED\" min=\"0\" max=\"1\" value=\"" + String(_configManager.getInt(ConfigManager::Param::SMS_SEND_DISABLED)) + "\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    // WiFi section
    html += "            <div class=\"section\">\n";
    html += "                <div class=\"section-title\">WiFi Credentials</div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>SSID: <input type=\"text\" name=\"WIFI_SSID\" value=\"" + _configManager.getStringParam(ConfigManager::Param::WIFI_SSID) + "\"></label>\n";
    html += "                </div>\n";
    html += "                <div class=\"form-group\">\n";
    html += "                    <label>Password: <input type=\"password\" name=\"WIFI_PWD\" placeholder=\"Leave empty to keep current\"></label>\n";
    html += "                </div>\n";
    html += "            </div>\n";

    html += "            <div style=\"display: grid; grid-template-columns: 1fr 1fr; gap: 5px;\">\n";
    html += "                <button type=\"submit\" style=\"grid-column: 1;\">Save</button>\n";
    html += "                <button type=\"button\" id=\"restartBtn\" style=\"grid-column: 2; background: #ff9800;\">Restart</button>\n";
    html += "                <button type=\"button\" id=\"weboffBtn\" style=\"grid-column: 1 / -1; background: #dc3545;\">Web Off</button>\n";
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
    html += "                if (value) params.append(key, value);\n";
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

