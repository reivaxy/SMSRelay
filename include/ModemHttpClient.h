#pragma once
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>

// Forward declarations
class Modem;

/**
 * @class ModemHttpClient
 * @brief Handles HTTPS requests through the 4G LTE mobile modem
 * 
 * This class provides a simple interface to send HTTP/HTTPS requests
 * using the modem's data connection. It supports GET and POST requests
 * with custom headers and automatic response handling.
 */
class ModemHttpClient {
public:
    /**
     * Constructor
     * @param modem Reference to the Modem instance
     */
    ModemHttpClient(Modem &modem);

    /**
     * Send an HTTPS GET request to the specified URL
     * @param url The full URL (e.g., "https://example.com/api/endpoint")
     * @param responseBuffer Buffer to store the response body
     * @param maxResponseSize Maximum size of the response buffer (default: 4096)
     * @return HTTP status code (e.g., 200 for success), or -1 on error
     */
    int httpGet(const String &url, String &responseBuffer, size_t maxResponseSize = 4096);

    /**
     * Send an HTTPS POST request to the specified URL
     * @param url The full URL (e.g., "https://example.com/api/endpoint")
     * @param postData The POST body data (JSON, form data, etc.)
     * @param responseBuffer Buffer to store the response body
     * @param contentType Content-Type header (default: "application/json")
     * @param maxResponseSize Maximum size of the response buffer (default: 4096)
     * @return HTTP status code (e.g., 200 for success), or -1 on error
     */
    int httpPost(const String &url, const String &postData, String &responseBuffer,
                 const String &contentType = "application/json", size_t maxResponseSize = 4096);

    /**
     * Send an HTTPS POST request with custom headers
     * @param url The full URL
     * @param postData The POST body data
     * @param headers Array of header strings (e.g., {"Authorization: Bearer token", "X-Custom: value"})
     * @param headerCount Number of headers in the array
     * @param responseBuffer Buffer to store the response body
     * @param contentType Content-Type header (default: "application/json")
     * @param maxResponseSize Maximum size of the response buffer (default: 4096)
     * @return HTTP status code, or -1 on error
     */
    int httpPostWithHeaders(const String &url, const String &postData, const String *headers, 
                           size_t headerCount, String &responseBuffer,
                           const String &contentType = "application/json", size_t maxResponseSize = 4096);

    /**
     * Send a GET request with custom headers
     * @param url The full URL
     * @param headers Array of header strings
     * @param headerCount Number of headers in the array
     * @param responseBuffer Buffer to store the response body
     * @param maxResponseSize Maximum size of the response buffer (default: 4096)
     * @return HTTP status code, or -1 on error
     */
    int httpGetWithHeaders(const String &url, const String *headers, size_t headerCount,
                          String &responseBuffer, size_t maxResponseSize = 4096);

    /**
     * Check if the modem is connected and ready for HTTP requests
     * @return true if ready, false otherwise
     */
    bool isReady() const;

    /**
     * Get the last error message
     * @return String describing the last error
     */
    String getLastError() const;

private:
    Modem &_modem;
    String _lastError;
    bool _sslConfigured;

    /**
     * Configure modem SSL settings to disable certificate validation
     * @return true if configuration successful or attempted
     */
    bool configureSslCertValidation();

    /**
     * Parse URL into components
     * @param url Full URL
     * @param host Will be set to hostname
     * @param port Will be set to port number
     * @param path Will be set to request path
     * @param useSSL Will be set to true if HTTPS
     * @return true if parsing succeeded, false otherwise
     */
    bool parseUrl(const String &url, String &host, int &port, String &path, bool &useSSL);

    /**
     * Read HTTP response from the client
     * @param client The connected client
     * @param statusCode Will be set to HTTP status code
     * @param responseBuffer Buffer for response body
     * @param maxSize Maximum response size
     * @return true if response was read successfully
     */
    bool readHttpResponse(TinyGsmClient &client, int &statusCode, String &responseBuffer, size_t maxSize);

    /**
     * Ensure GPRS/data connection is active before HTTP requests
     * Attempts to activate GPRS if not already connected
     * @return true if GPRS is connected, false on failure
     */
    bool ensureGprsConnected();

    /**
     * Perform an HTTPS request using the modem's built-in HTTP(S) service (AT+HTTP*).
     * The TinyGSM socket client does not support TLS on A76xx modems.
     * @return HTTP status code, or -1 on error
     */
    int httpsRequest(bool isPost, const String &url, const String &postData,
                     const String *headers, size_t headerCount,
                     const String &contentType, String &responseBuffer, size_t maxResponseSize);
};
