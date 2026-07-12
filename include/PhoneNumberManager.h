#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "secret.h"

// Manages authorized phone numbers with different permission levels.
// The root phone number (from ROOT_NUMBER) is always admin.
// Up to 5 additional phone numbers can be added with either admin or read-only permissions.
// Permissions:
//   - ADMIN: Can execute all commands (status, read, write, config, add/remove phones)
//   - READ:  Can only read (status, levels, config), receives alerts
class PhoneNumberManager {
public:
    enum class Permission {
        NONE,   // Not authorized
        READ,   // Read-only (status, levels, config)
        ADMIN   // Full access
    };

    struct AuthorizedNumber {
        String number;
        Permission permission;
    };

    PhoneNumberManager(const String &rootNumber);
    ~PhoneNumberManager();

    // Initialize preferences storage
    void init();

    // Check if a phone number is authorized and return its permission level
    Permission getPermission(const String &number);

    // Get the root phone number (always admin)
    String getRootNumber() const { return _rootNumber; }

    // Add a new authorized phone number (max 5 additional numbers)
    // Optionally accepts an alias for display purposes
    // Returns true if added successfully, false if already exists or max reached
    bool addPhoneNumber(const String &number, Permission permission, const String &alias = "");

    // Remove an authorized phone number (cannot remove root)
    // Accepts either a phone number or an index (0=root, 1-5=additional)
    // Returns true if removed, false if not found or is root
    bool removePhoneNumber(const String &numberOrIndex);

    // Get all authorized phone numbers (including root with ADMIN permission)
    // Returns vector of AuthorizedNumber structs
    std::vector<AuthorizedNumber> getAllNumbers();

    // Get formatted string of all authorized numbers with indices
    String getFormattedList();

    // Clear all non-root phone numbers (for debugging/reset)
    void clearAllNonRoot();

    // Get max number of additional phone numbers allowed
    static constexpr int MAX_ADDITIONAL_NUMBERS = 5;

    // Mute management - muted numbers won't receive alerts but can still send commands
    // Check if a phone number is muted
    bool isMuted(const String &number);

    // Mute a phone number (won't receive alerts, but can still send commands)
    // Accepts either a phone number or an index (0=root, 1-5=additional)
    bool muteNumber(const String &numberOrIndex);

    // Unmute a phone number
    // Accepts either a phone number or an index (0=root, 1-5=additional)
    bool unmuteNumber(const String &numberOrIndex);

    // Get mute status for all authorized numbers
    String getMuteStatusList();

    // Get the alias for a phone number
    String getAlias(const String &number);

private:
    Preferences _prefs;
    String _rootNumber;

    // Get the preference key for a phone number slot (0-4)
    static String getPhoneKey(int index);
    static String getPermissionKey(int index);
    static String getMuteKey(int index);
    static String getAliasKey(int index);

    // Normalize phone number (remove spaces, ensure consistent format)
    static String normalizeNumber(const String &number);

    // Find a phone number slot, returns -1 if not found or slot index if found
    int findPhoneSlot(const String &number);

    // Resolve a phone number from either an index (0-5) or a phone number string
    // Returns the normalized phone number, or empty string if not found
    String resolvePhoneByIndexOrNumber(const String &indexOrNumber);
};
