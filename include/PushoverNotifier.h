#pragma once
#include <Arduino.h>

// Forward declarations
class ConfigManager;
class ModemHttpClient;

/**
 * @class PushoverNotifier
 * @brief Sends push notifications via the Pushover service
 * 
 * This class handles sending notifications to the Pushover push notification service
 * (https://pushover.net) using the 4G LTE modem's data connection.
 * 
 * Requires two configuration parameters:
 * - PUSHOVER_USER_TOKEN: User token from Pushover account
 * - PUSHOVER_API_TOKEN: Application token for this device
 */
class PushoverNotifier {
public:
    /**
     * Constructor
     * @param configManager Reference to ConfigManager for storing API credentials
     * @param httpClient Reference to ModemHttpClient for sending HTTPS requests
     */
    PushoverNotifier(ConfigManager &configManager, ModemHttpClient &httpClient);

    /**
     * Send a notification to Pushover
     * @param title Notification title
     * @param message Notification message
     * @param priority Optional priority level (-2 to 2, default 0)
     *   -2: Lowest priority
     *   -1: Low priority
     *    0: Normal priority (default)
     *    1: High priority
     *    2: Emergency priority (requires retry and expire parameters)
     * @return true if notification was sent successfully, false otherwise
     */
    bool send(const String &title, const String &message, int priority = 0);

    /**
     * Send a notification with full parameters
     * @param title Notification title
     * @param message Notification message
     * @param priority Priority level (-2 to 2)
     * @param url URL to include in notification
     * @param urlTitle Title for the URL
     * @param sound Sound to play (see Pushover documentation for available sounds)
     * @return true if sent successfully
     */
    bool sendFull(const String &title, const String &message, int priority = 0,
                  const String &url = "", const String &urlTitle = "", const String &sound = "");

    /**
     * Check if the notifier is properly configured (credentials are set)
     * @return true if both user token and API token are configured
     */
    bool isConfigured() const;

    /**
     * Get the last error message
     * @return String describing the last error
     */
    String getLastError() const;

private:
    ConfigManager &_configManager;
    ModemHttpClient &_httpClient;
    String _lastError;

    /**
     * Build JSON payload for Pushover API
     * @param title Notification title
     * @param message Notification message
     * @param priority Priority level
     * @param url Optional URL
     * @param urlTitle Optional URL title
     * @param sound Optional sound
     * @return JSON string ready for POST to Pushover API
     */
    String buildJsonPayload(const String &title, const String &message, int priority,
                           const String &url, const String &urlTitle, const String &sound) const;
};
