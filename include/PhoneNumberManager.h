#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <vector>

// Manages authorized phone numbers with different permission levels.
// The root phone number (from SMS_TARGET) is always admin.
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
    // Returns true if added successfully, false if already exists or max reached
    bool addPhoneNumber(const String &number, Permission permission);

    // Remove an authorized phone number (cannot remove root)
    // Returns true if removed, false if not found or is root
    bool removePhoneNumber(const String &number);

    // Get all authorized phone numbers (including root with ADMIN permission)
    // Returns vector of AuthorizedNumber structs
    std::vector<AuthorizedNumber> getAllNumbers();

    // Get formatted string of all authorized numbers
    String getFormattedList();

    // Clear all non-root phone numbers (for debugging/reset)
    void clearAllNonRoot();

    // Get max number of additional phone numbers allowed
    static constexpr int MAX_ADDITIONAL_NUMBERS = 5;

private:
    Preferences _prefs;
    String _rootNumber;

    // Get the preference key for a phone number slot (0-4)
    static String getPhoneKey(int index);
    static String getPermissionKey(int index);

    // Normalize phone number (remove spaces, ensure consistent format)
    static String normalizeNumber(const String &number);

    // Find a phone number slot, returns -1 if not found or slot index if found
    int findPhoneSlot(const String &number);
};
