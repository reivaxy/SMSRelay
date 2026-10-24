#include "PushoverNotifier.h"
#include "ConfigManager.h"
#include "ModemHttpClient.h"
#include <ArduinoJson.h>

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

String PushoverNotifier::buildJsonPayload(const String &title, const String &message, int priority,
                                         const String &url, const String &urlTitle, const String &sound) const {
    JsonDocument doc;

    doc["user"] = _configManager.getStringParam(ConfigManager::Param::PUSHOVER_USER_TOKEN);
    doc["token"] = _configManager.getStringParam(ConfigManager::Param::PUSHOVER_API_TOKEN);
    doc["title"] = title;
    doc["message"] = message;
    doc["priority"] = priority;

    if (url.length() > 0) {
        doc["url"] = url;
    }

    if (urlTitle.length() > 0) {
        doc["url_title"] = urlTitle;
    }

    if (sound.length() > 0) {
        doc["sound"] = sound;
    }

    String json;
    serializeJson(doc, json);
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
    int statusCode = _httpClient.httpPost(
        "https://api.pushover.net/1/messages.json",
        jsonPayload,
        response,
        "application/json",
        2048
    );

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
