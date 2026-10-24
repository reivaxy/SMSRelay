#include "ModemHttpClient.h"
#include "Modem.h"

ModemHttpClient::ModemHttpClient(Modem &modem)
    : _modem(modem), _lastError(""), _sslConfigured(false) {
}

bool ModemHttpClient::isReady() const {
    // GPRS is intentionally not checked here: requests call ensureGprsConnected(),
    // which (re)activates the data connection on demand.
    return _modem.isInitialized() && _modem.isConnected();
}

bool ModemHttpClient::configureSslCertValidation() {
    // Configure modem to skip SSL certificate validation
    if (_sslConfigured) {
        log_d("ModemHttpClient: SSL already configured");
        return true;
    }

    // Send AT command to disable SSL certificate validation on A7670 modem
    // AT+SSLCONFIG=<sslId>,<sslVersion>,<certificateType>,<ignoreCertificateMisMatch>
    // Setting ignoreCertificateMisMatch=1 to disable validation
    log_i("ModemHttpClient: Configuring SSL to skip certificate validation...");
    
    // Try to send the AT command through the modem
    TinyGsm &modemObj = _modem.getModem();
    
    // Send AT+SSLCONFIG command to disable cert validation
    // Format: AT+SSLCONFIG=0,0,0,1 (id=0, version=auto, certType=CA, ignoreMismatch=1)
    modemObj.sendAT("+SSLCONFIG=0,0,0,1");
    
    // Check response - should be OK
    if (modemObj.waitResponse() == 1) {
        log_i("ModemHttpClient: SSL certificate validation disabled successfully");
        _sslConfigured = true;
        return true;
    } else {
        log_w("ModemHttpClient: Failed to configure SSL - modem may not support AT+SSLCONFIG");
        // Don't fail - some modems don't support this command
        _sslConfigured = true;  // Mark as attempted
        return true;
    }
}

bool ModemHttpClient::ensureGprsConnected() {
    // Check if GPRS is already connected
    if (_modem.isGprsConnected()) {
        log_d("ModemHttpClient: GPRS already connected");
        return true;
    }

    // GPRS not connected, attempt to activate it through Modem (with retries, as the
    // data connection can take a few seconds to come up, e.g. after the modem was busy
    // sending SMS or the network just came back)
    log_w("ModemHttpClient: GPRS not connected, attempting activation...");
    const int maxAttempts = 3;
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        if (_modem.activateGprs()) {
            log_i("ModemHttpClient: GPRS connection activated (attempt %d)", attempt);
            return true;
        }
        log_w("ModemHttpClient: GPRS activation attempt %d/%d failed", attempt, maxAttempts);
        if (attempt < maxAttempts) {
            delay(3000);
        }
    }

    _lastError = "Failed to activate GPRS data connection";
    log_e("ModemHttpClient: %s", _lastError.c_str());
    return false;
}

String ModemHttpClient::getLastError() const {
    return _lastError;
}

bool ModemHttpClient::parseUrl(const String &url, String &host, int &port, String &path, bool &useSSL) {
    // Parse URL format: https://example.com:443/path/to/resource or http://example.com/path
    
    int protocolEnd = url.indexOf("://");
    if (protocolEnd == -1) {
        _lastError = "Invalid URL format: missing ://";
        return false;
    }

    String protocol = url.substring(0, protocolEnd);
    protocol.toLowerCase();
    useSSL = (protocol == "https");

    int pathStart = url.indexOf("/", protocolEnd + 3);
    if (pathStart == -1) {
        // No path specified, use root
        host = url.substring(protocolEnd + 3);
        path = "/";
    } else {
        host = url.substring(protocolEnd + 3, pathStart);
        path = url.substring(pathStart);
    }

    // Extract port if specified (e.g., example.com:8080)
    int portSeparator = host.indexOf(":");
    if (portSeparator != -1) {
        String portStr = host.substring(portSeparator + 1);
        port = portStr.toInt();
        host = host.substring(0, portSeparator);
    } else {
        port = useSSL ? 443 : 80;
    }

    if (host.length() == 0) {
        _lastError = "Invalid URL: empty host";
        return false;
    }

    return true;
}

bool ModemHttpClient::readHttpResponse(TinyGsmClient &client, int &statusCode, String &responseBuffer, size_t maxSize) {
    statusCode = -1;
    responseBuffer = "";
    
    // Give the server time to send the response
    // On slow connections, data may not arrive immediately
    delay(100);
    
    // Read the status line
    String statusLine = "";
    unsigned long timeout = millis() + 15000;  // 15 second timeout for response
    
    // Wait for data to arrive (initial wait up to 2 seconds)
    unsigned long waitStart = millis();
    while (!client.available() && (millis() - waitStart) < 2000) {
        delay(10);
    }
    
    if (!client.available()) {
        _lastError = "No response from server";
        log_e("ModemHttpClient: No response available from server");
        return false;
    }
    
    // Read status line
    while (client.available() && millis() < timeout) {
        char c = client.read();
        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            statusLine += c;
        }
    }

    log_d("ModemHttpClient: Status line: [%s]", statusLine.c_str());

    if (statusLine.length() == 0) {
        _lastError = "Empty HTTP response - no status line received";
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return false;
    }

    // Parse status code from status line (e.g., "HTTP/1.1 200 OK")
    int codeStart = statusLine.indexOf(" ");
    if (codeStart == -1) {
        _lastError = "Invalid HTTP response: cannot find status code in: " + statusLine;
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return false;
    }
    
    int codeEnd = statusLine.indexOf(" ", codeStart + 1);
    if (codeEnd == -1) {
        _lastError = "Invalid HTTP response: malformed status line: " + statusLine;
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return false;
    }
    
    String codeStr = statusLine.substring(codeStart + 1, codeEnd);
    statusCode = codeStr.toInt();
    log_d("ModemHttpClient: HTTP status code: %d", statusCode);

    // Read headers until we find an empty line (end of headers)
    while (client.available() && millis() < timeout) {
        String headerLine = "";
        while (client.available()) {
            char c = client.read();
            if (c == '\n') {
                break;
            }
            if (c != '\r') {
                headerLine += c;
            }
        }
        
        if (headerLine.length() == 0) {
            // Empty line marks end of headers
            break;
        }
    }

    // Read response body
    size_t bytesRead = 0;
    while (client.available() && bytesRead < maxSize && millis() < timeout) {
        char c = client.read();
        responseBuffer += c;
        bytesRead++;
    }

    log_d("ModemHttpClient: Response body length: %u bytes", bytesRead);
    return true;
}

int ModemHttpClient::httpsRequest(bool isPost, const String &url, const String &postData,
                                  const String *headers, size_t headerCount,
                                  const String &contentType, String &responseBuffer,
                                  size_t maxResponseSize) {
    // The TinyGSM socket client (AT+CIPOPEN) has no TLS support on A76xx modems,
    // so HTTPS must go through the modem's built-in HTTP(S) service (AT+HTTP*).
    TinyGsm &modem = _modem.getModem();
    responseBuffer = "";

    // The modem HTTP service can fail DNS resolution (status 713) when the carrier
    // doesn't provide DNS servers to it, so set public DNS servers explicitly.
    modem.sendAT("+CDNSCFG=\"8.8.8.8\",\"1.1.1.1\"");
    if (modem.waitResponse(3000) != 1) {
        log_w("ModemHttpClient: Failed to set DNS servers (AT+CDNSCFG)");
    }

    if (!modem.https_begin()) {
        _lastError = "Failed to initialise modem HTTPS service";
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }

    if (!modem.https_set_url(url)) {
        modem.https_end();
        _lastError = "Failed to set HTTPS URL";
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }

    if (isPost && contentType.length() > 0) {
        modem.https_set_content_type(contentType.c_str());
    }

    for (size_t i = 0; i < headerCount; i++) {
        modem.sendAT("+HTTPPARA=\"USERDATA\",\"", headers[i], "\"");
        modem.waitResponse(3000);
    }

    log_i("ModemHttpClient: Performing HTTPS %s via modem stack...", isPost ? "POST" : "GET");
    int statusCode = isPost ? modem.https_post(postData) : modem.https_get();

    // Status >= 600 are modem-level errors (network/DNS/SSL), 0 or negative means no result
    if (statusCode <= 0 || statusCode >= 600) {
        modem.https_end();
        const char *reason = "unknown";
        switch (statusCode) {
            case 701: reason = "alert state"; break;
            case 703: reason = "busy"; break;
            case 704: reason = "connection closed"; break;
            case 705: reason = "timeout"; break;
            case 706: reason = "socket send/receive failed"; break;
            case 709: reason = "network error"; break;
            case 710: reason = "SSL session start failed"; break;
            case 711: reason = "wrong state"; break;
            case 712: reason = "socket creation failed"; break;
            case 713: reason = "DNS lookup failed"; break;
            case 714: reason = "socket connect failed"; break;
            case 715: reason = "SSL handshake failed"; break;
            case 717: reason = "no network"; break;
            case 718: reason = "send timeout"; break;
            case 719: reason = "CA certificate missing"; break;
            default: break;
        }
        _lastError = "HTTPS request failed, modem status: " + String(statusCode) + " (" + reason + ")";
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }

    String body = modem.https_body();
    if (body.length() > maxResponseSize) {
        body = body.substring(0, maxResponseSize);
    }
    responseBuffer = body;
    modem.https_end();

    log_d("ModemHttpClient: HTTPS response code: %d, body length: %u", statusCode, (unsigned)responseBuffer.length());
    return statusCode;
}

int ModemHttpClient::httpGet(const String &url, String &responseBuffer, size_t maxResponseSize) {
    if (!_modem.isInitialized() || !_modem.isConnected()) {
        _lastError = "Modem not connected";
        return -1;
    }

    // Ensure GPRS is connected for data operations
    if (!ensureGprsConnected()) {
        return -1;
    }

    String host, path;
    int port;
    bool useSSL;

    if (!parseUrl(url, host, port, path, useSSL)) {
        return -1;
    }

    log_i("ModemHttpClient: GET %s:%d%s (SSL: %d)", host.c_str(), port, path.c_str(), useSSL);

    if (useSSL) {
        return httpsRequest(false, url, "", nullptr, 0, "", responseBuffer, maxResponseSize);
    }

    TinyGsmClient client(_modem.getModem(), 1);  // Profile 1 for HTTP
    
    log_i("ModemHttpClient: Connecting to %s:%d with SSL=%d...", host.c_str(), port, useSSL);
    if (!client.connect(host.c_str(), port, useSSL)) {
        _lastError = "Failed to connect to host: " + host + ":" + String(port);
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }

    // Give the SSL handshake time to complete if HTTPS
    // Increased to 2000ms (2 seconds) to allow full SSL/TLS handshake and certificate processing
    if (useSSL) {
        log_d("ModemHttpClient: SSL connection established, waiting for handshake completion (2000ms)...");
        delay(2000);  // Allow SSL/TLS handshake to complete
    }

    // Send HTTP GET request
    String request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "User-Agent: ESP32-ModemHttpClient\r\n";
    request += "\r\n";

    client.print(request);

    int statusCode;
    bool success = readHttpResponse(client, statusCode, responseBuffer, maxResponseSize);
    client.stop();

    if (!success) {
        return -1;
    }

    log_d("ModemHttpClient: GET response code: %d", statusCode);
    return statusCode;
}

int ModemHttpClient::httpPost(const String &url, const String &postData, String &responseBuffer,
                              const String &contentType, size_t maxResponseSize) {
    if (!_modem.isInitialized() || !_modem.isConnected()) {
        _lastError = "Modem not connected";
        return -1;
    }

    // Ensure GPRS is connected for data operations
    if (!ensureGprsConnected()) {
        return -1;
    }

    String host, path;
    int port;
    bool useSSL;

    if (!parseUrl(url, host, port, path, useSSL)) {
        return -1;
    }

    log_i("ModemHttpClient: POST %s:%d%s (SSL: %d)", host.c_str(), port, path.c_str(), useSSL);

    if (useSSL) {
        return httpsRequest(true, url, postData, nullptr, 0, contentType, responseBuffer, maxResponseSize);
    }

    // For HTTPS connections, we need to use port 443 and enable SSL in TinyGsmClient
    // Create client with the correct profile
    // Profile 0-2 can be used, using profile 1 for data connections
    TinyGsmClient client(_modem.getModem(), 1);
    
    log_i("ModemHttpClient: Connecting to %s:%d with SSL=%d...", host.c_str(), port, useSSL);
    if (!client.connect(host.c_str(), port, useSSL)) {
        _lastError = "Failed to connect to host: " + host + ":" + String(port);
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }
    
    // Give the SSL handshake time to complete if HTTPS
    // Increased to 2000ms (2 seconds) to allow full SSL/TLS handshake and certificate processing
    if (useSSL) {
        log_d("ModemHttpClient: SSL connection established, waiting for handshake completion (2000ms)...");
        delay(2000);  // Allow SSL/TLS handshake to complete
    }

    // Send HTTP POST request
    String request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "Content-Type: " + contentType + "\r\n";
    request += "Content-Length: " + String(postData.length()) + "\r\n";
    request += "User-Agent: ESP32-ModemHttpClient\r\n";
    request += "\r\n";

    client.print(request);
    client.print(postData);

    int statusCode;
    bool success = readHttpResponse(client, statusCode, responseBuffer, maxResponseSize);
    client.stop();

    if (!success) {
        return -1;
    }

    log_d("ModemHttpClient: POST response code: %d", statusCode);
    return statusCode;
}

int ModemHttpClient::httpGetWithHeaders(const String &url, const String *headers, size_t headerCount,
                                        String &responseBuffer, size_t maxResponseSize) {
    if (!_modem.isInitialized() || !_modem.isConnected()) {
        _lastError = "Modem not connected";
        return -1;
    }

    // Ensure GPRS is connected for data operations
    if (!ensureGprsConnected()) {
        return -1;
    }

    String host, path;
    int port;
    bool useSSL;

    if (!parseUrl(url, host, port, path, useSSL)) {
        return -1;
    }

    log_i("ModemHttpClient: GET with headers %s:%d%s (SSL: %d)", host.c_str(), port, path.c_str(), useSSL);

    if (useSSL) {
        return httpsRequest(false, url, "", headers, headerCount, "", responseBuffer, maxResponseSize);
    }

    TinyGsmClient client(_modem.getModem(), 1);
    
    log_i("ModemHttpClient: Connecting to %s:%d with SSL=%d...", host.c_str(), port, useSSL);
    if (!client.connect(host.c_str(), port, useSSL)) {
        _lastError = "Failed to connect to host: " + host + ":" + String(port);
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }

    // Give the SSL handshake time to complete if HTTPS
    // Increased to 2000ms (2 seconds) to allow full SSL/TLS handshake and certificate processing
    if (useSSL) {
        log_d("ModemHttpClient: SSL connection established, waiting for handshake completion (2000ms)...");
        delay(2000);  // Allow SSL/TLS handshake to complete
    }

    // Send HTTP GET request with custom headers
    String request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "User-Agent: ESP32-ModemHttpClient\r\n";
    
    for (size_t i = 0; i < headerCount; i++) {
        request += headers[i] + "\r\n";
    }
    
    request += "\r\n";

    client.print(request);

    int statusCode;
    bool success = readHttpResponse(client, statusCode, responseBuffer, maxResponseSize);
    client.stop();

    if (!success) {
        return -1;
    }

    log_d("ModemHttpClient: GET response code: %d", statusCode);
    return statusCode;
}

int ModemHttpClient::httpPostWithHeaders(const String &url, const String &postData, const String *headers,
                                        size_t headerCount, String &responseBuffer,
                                        const String &contentType, size_t maxResponseSize) {
    if (!_modem.isInitialized() || !_modem.isConnected()) {
        _lastError = "Modem not connected";
        return -1;
    }

    // Ensure GPRS is connected for data operations
    if (!ensureGprsConnected()) {
        return -1;
    }

    String host, path;
    int port;
    bool useSSL;

    if (!parseUrl(url, host, port, path, useSSL)) {
        return -1;
    }

    log_i("ModemHttpClient: POST with headers %s:%d%s (SSL: %d)", host.c_str(), port, path.c_str(), useSSL);

    if (useSSL) {
        return httpsRequest(true, url, postData, headers, headerCount, contentType, responseBuffer, maxResponseSize);
    }

    TinyGsmClient client(_modem.getModem(), 1);
    
    log_i("ModemHttpClient: Connecting to %s:%d with SSL=%d...", host.c_str(), port, useSSL);
    if (!client.connect(host.c_str(), port, useSSL)) {
        _lastError = "Failed to connect to host: " + host + ":" + String(port);
        log_e("ModemHttpClient: %s", _lastError.c_str());
        return -1;
    }

    // Give the SSL handshake time to complete if HTTPS
    // Increased to 2000ms (2 seconds) to allow full SSL/TLS handshake and certificate processing
    if (useSSL) {
        log_d("ModemHttpClient: SSL connection established, waiting for handshake completion (2000ms)...");
        delay(2000);  // Allow SSL/TLS handshake to complete
    }

    // Send HTTP POST request with custom headers
    String request = "POST " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "Content-Type: " + contentType + "\r\n";
    request += "Content-Length: " + String(postData.length()) + "\r\n";
    request += "User-Agent: ESP32-ModemHttpClient\r\n";
    
    for (size_t i = 0; i < headerCount; i++) {
        request += headers[i] + "\r\n";
    }
    
    request += "\r\n";

    client.print(request);
    client.print(postData);

    int statusCode;
    bool success = readHttpResponse(client, statusCode, responseBuffer, maxResponseSize);
    client.stop();

    if (!success) {
        return -1;
    }

    log_d("ModemHttpClient: POST response code: %d", statusCode);
    return statusCode;
}
