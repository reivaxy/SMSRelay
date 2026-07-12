#include "PhoneNumberManager.h"
#include "utilities.h"

PhoneNumberManager::PhoneNumberManager(const String &rootNumber)
    : _rootNumber(normalizeNumber(rootNumber)) {}

PhoneNumberManager::~PhoneNumberManager() {
    _prefs.end();
}

void PhoneNumberManager::init() {
    if (!_prefs.begin("phoneNumbers", false)) {
        log_e("[PhoneNumberManager] Failed to initialize Preferences");
    }
    log_i("[PhoneNumberManager] Initialized with root: %s", _rootNumber.c_str());
}

PhoneNumberManager::Permission PhoneNumberManager::getPermission(const String &number) {
    String normalized = normalizeNumber(number);
    
    // Root number is always admin
    if (normalized == _rootNumber) {
        return Permission::ADMIN;
    }

    // Check stored phone numbers
    int slot = findPhoneSlot(normalized);
    if (slot >= 0) {
        int permValue = _prefs.getInt(getPermissionKey(slot).c_str(), (int)Permission::NONE);
        return (Permission)permValue;
    }

    return Permission::NONE;
}

bool PhoneNumberManager::addPhoneNumber(const String &number, Permission permission) {
    String normalized = normalizeNumber(number);

    // Cannot add root number
    if (normalized == _rootNumber) {
        log_w("[PhoneNumberManager] Cannot add root number");
        return false;
    }

    // Check if already exists
    if (findPhoneSlot(normalized) >= 0) {
        log_w("[PhoneNumberManager] Number already exists: %s", normalized.c_str());
        return false;
    }

    // Find first empty slot
    for (int i = 0; i < MAX_ADDITIONAL_NUMBERS; i++) {
        String storedNumber = _prefs.getString(getPhoneKey(i).c_str(), "");
        if (storedNumber.length() == 0) {
            _prefs.putString(getPhoneKey(i).c_str(), normalized);
            _prefs.putInt(getPermissionKey(i).c_str(), (int)permission);
            log_i("[PhoneNumberManager] Added %s with permission %d", normalized.c_str(), (int)permission);
            return true;
        }
    }

    log_w("[PhoneNumberManager] Maximum phone numbers reached");
    return false;
}

bool PhoneNumberManager::removePhoneNumber(const String &number) {
    String normalized = normalizeNumber(number);

    // Cannot remove root number
    if (normalized == _rootNumber) {
        log_w("[PhoneNumberManager] Cannot remove root number");
        return false;
    }

    int slot = findPhoneSlot(normalized);
    if (slot >= 0) {
        _prefs.remove(getPhoneKey(slot).c_str());
        _prefs.remove(getPermissionKey(slot).c_str());
        log_i("[PhoneNumberManager] Removed %s", normalized.c_str());
        return true;
    }

    log_w("[PhoneNumberManager] Number not found: %s", normalized.c_str());
    return false;
}

std::vector<PhoneNumberManager::AuthorizedNumber> PhoneNumberManager::getAllNumbers() {
    std::vector<AuthorizedNumber> result;

    // Add root number
    result.push_back({_rootNumber, Permission::ADMIN});

    // Add stored numbers
    for (int i = 0; i < MAX_ADDITIONAL_NUMBERS; i++) {
        String storedNumber = _prefs.getString(getPhoneKey(i).c_str(), "");
        if (storedNumber.length() > 0) {
            int permValue = _prefs.getInt(getPermissionKey(i).c_str(), (int)Permission::NONE);
            result.push_back({storedNumber, (Permission)permValue});
        }
    }

    return result;
}

String PhoneNumberManager::getFormattedList() {
    String result = "Authorized numbers:\n";
    auto numbers = getAllNumbers();
    
    for (const auto &entry : numbers) {
        String permStr = (entry.permission == Permission::ADMIN) ? "ADMIN" : "READ";
        result += entry.number + " (" + permStr + ")\n";
    }

    return result;
}

void PhoneNumberManager::clearAllNonRoot() {
    for (int i = 0; i < MAX_ADDITIONAL_NUMBERS; i++) {
        _prefs.remove(getPhoneKey(i).c_str());
        _prefs.remove(getPermissionKey(i).c_str());
    }
    log_i("[PhoneNumberManager] Cleared all non-root numbers");
}

String PhoneNumberManager::getPhoneKey(int index) {
    return "phone_" + String(index);
}

String PhoneNumberManager::getPermissionKey(int index) {
    return "perm_" + String(index);
}

String PhoneNumberManager::normalizeNumber(const String &number) {
    String normalized = number;
    normalized.trim();
    // Remove spaces and normalize
    normalized.replace(" ", "");
    // Ensure it starts with +
    if (normalized.length() > 0 && normalized[0] != '+') {
        normalized = "+" + normalized;
    }
    return normalized;
}

int PhoneNumberManager::findPhoneSlot(const String &number) {
    String normalized = normalizeNumber(number);
    for (int i = 0; i < MAX_ADDITIONAL_NUMBERS; i++) {
        String storedNumber = _prefs.getString(getPhoneKey(i).c_str(), "");
        if (storedNumber == normalized) {
            return i;
        }
    }
    return -1;
}
