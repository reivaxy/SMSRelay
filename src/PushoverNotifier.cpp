#include "PushoverNotifier.h"
#include "ConfigManager.h"
#include "ModemHttpClient.h"

PushoverNotifier::PushoverNotifier(ConfigManager &configManager, ModemHttpClient &httpClient)
    : _configManager(configManager), _httpClient(httpClient), _lastError("") {
}

bool PushoverNotifier::isConfigured() const {
    String userToken = _configManager.getStringParam(ConfigManager::Param::PUSHOVER_USER_TOKEN);
    String apiToken = _configManager.getStringParam(ConfigManager::Param::PUSHOVER_API_TOKEN);
    return userToken.length() > 0 && apiToken.length() > 0;
}

String PushoverNotifier::getLastError() const {
    return _lastError;
}

// Helper function to escape JSON special characters
static String escapeJsonString(const String &input) {
    String output = "";
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (c < 0x20) {
                    // Control characters - escape as \uXXXX
                    char hex[7];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)c);
                    output += hex;
                } else {
                    output += c;
                }
        }
    }
    return output;
}

String PushoverNotifier::buildJsonPayload(const String &title, const String &message, int priority,
                                         const String &url, const String &urlTitle, const String &sound) const {
    // Build JSON manually to avoid external JSON library
    String json = "{";
    
    // Add user and API tokens
    String userToken = _configManager.getStringParam(ConfigManager::Param::PUSHOVER_USER_TOKEN);
    String apiToken = _configManager.getStringParam(ConfigManager::Param::PUSHOVER_API_TOKEN);
    
    json += "\"user\":\"" + escapeJsonString(userToken) + "\",";
    json += "\"token\":\"" + escapeJsonString(apiToken) + "\",";
    json += "\"title\":\"" + escapeJsonString(title) + "\",";
    json += "\"message\":\"" + escapeJsonString(message) + "\",";
    json += "\"priority\":" + String(priority);
    
    if (url.length() > 0) {
        json += ",\"url\":\"" + escapeJsonString(url) + "\"";
    }
    
    if (urlTitle.length() > 0) {
        json += ",\"url_title\":\"" + escapeJsonString(urlTitle) + "\"";
    }
    
    if (sound.length() > 0) {
        json += ",\"sound\":\"" + escapeJsonString(sound) + "\"";
    }
    
    json += "}";
    
    log_d("PushoverNotifier: JSON payload: %s", json.c_str());
    return json;
}

bool PushoverNotifier::send(const String &title, const String &message, int priority) {
    return sendFull(title, message, priority, "", "", "");
}

bool PushoverNotifier::sendFull(const String &title, const String &message, int priority,
                               const String &url, const String &urlTitle, const String &sound) {
    if (!isConfigured()) {
        _lastError = "Pushover not configured - set user token and API token";
        log_e("PushoverNotifier: %s", _lastError.c_str());
        return false;
    }

    if (!_httpClient.isReady()) {
        _lastError = "Modem not connected";
        log_e("PushoverNotifier: %s", _lastError.c_str());
        return false;
    }

    // Validate priority
    if (priority < -2 || priority > 2) {
        priority = 0;  // Default to normal priority
    }

    String jsonPayload = buildJsonPayload(title, message, priority, url, urlTitle, sound);

    log_i("PushoverNotifier: Sending notification - Title: %s, Message length: %d", title.c_str(), message.length());
    log_d("PushoverNotifier: JSON payload size: %d bytes", jsonPayload.length());

    String response;
    // Try HTTPS first, fall back to HTTP if it fails
    int statusCode = _httpClient.httpPost(
        "https://api.pushover.net/1/messages.json",
        jsonPayload,
        response,
        "application/json",
        2048
    );
    
    // If HTTPS fails with connection error, try HTTP as fallback
    if (statusCode == -1 && _httpClient.getLastError().indexOf("Failed to connect") != -1) {
        log_w("PushoverNotifier: HTTPS connection failed, attempting HTTP fallback...");
        statusCode = _httpClient.httpPost(
            "http://api.pushover.net/1/messages.json",
            jsonPayload,
            response,
            "application/json",
            2048
        );
        if (statusCode != 200) {
            log_e("PushoverNotifier: HTTP fallback also failed");
        } else {
            log_w("PushoverNotifier: HTTP fallback succeeded - HTTPS is not supported by modem");
        }
    }

    if (statusCode != 200) {
        _lastError = "HTTP error: " + String(statusCode);
        log_e("PushoverNotifier: Send failed - %s", _lastError.c_str());
        log_e("PushoverNotifier: ModemHttpClient error: %s", _httpClient.getLastError().c_str());
        if (response.length() > 0) {
            log_e("PushoverNotifier: Response body: %s", response.c_str());
        }
        return false;
    }

    // Parse response to check for Pushover API errors
    // Pushover returns 200 with error details in JSON if there's an issue
    if (response.indexOf("\"status\":1") == -1) {
        // Not a successful response
        _lastError = "Pushover API error: " + response;
        log_e("PushoverNotifier: %s", _lastError.c_str());
        return false;
    }

    log_i("PushoverNotifier: Notification sent successfully");
    return true;
}
