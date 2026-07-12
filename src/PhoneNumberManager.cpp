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

bool PhoneNumberManager::addPhoneNumber(const String &number, Permission permission, const String &alias) {
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
            if (alias.length() > 0) {
                _prefs.putString(getAliasKey(i).c_str(), alias);
            }
            log_i("[PhoneNumberManager] Added %s with permission %d", normalized.c_str(), (int)permission);
            return true;
        }
    }

    log_w("[PhoneNumberManager] Maximum phone numbers reached");
    return false;
}

bool PhoneNumberManager::removePhoneNumber(const String &numberOrIndex) {
    String normalized = normalizeNumber(numberOrIndex);

    // Cannot remove root number
    if (normalized == _rootNumber) {
        log_w("[PhoneNumberManager] Cannot remove root number");
        return false;
    }

    int slot = findPhoneSlot(normalized);
    if (slot >= 0) {
        _prefs.remove(getPhoneKey(slot).c_str());
        _prefs.remove(getPermissionKey(slot).c_str());
        _prefs.remove(getMuteKey(slot).c_str());
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
    
    for (size_t i = 0; i < numbers.size(); i++) {
        String permStr = (numbers[i].permission == Permission::ADMIN) ? "ADMIN" : "READ";
        String muteStr = isMuted(numbers[i].number) ? " [MUTED]" : "";
        String alias = getAlias(numbers[i].number);
        String aliasStr = (alias.length() > 0) ? " (" + alias + ")" : "";
        result += "[" + String(i) + "] " + numbers[i].number + aliasStr + " (" + permStr + ")" + muteStr + "\n";
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

String PhoneNumberManager::getMuteKey(int index) {
    return "mute_" + String(index);
}

String PhoneNumberManager::getAliasKey(int index) {
    return "alias_" + String(index);
}

String PhoneNumberManager::getAlias(const String &number) {
    String normalized = normalizeNumber(number);

    // Root number has no alias
    if (normalized == _rootNumber) {
        return "";
    }

    int slot = findPhoneSlot(normalized);
    if (slot >= 0) {
        return _prefs.getString(getAliasKey(slot).c_str(), "");
    }

    return "";
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

String PhoneNumberManager::resolvePhoneByIndexOrNumber(const String &indexOrNumber) {
    String trimmed = indexOrNumber;
    trimmed.trim();
    
    // Try to parse as index (0-5)
    bool isNumeric = true;
    for (size_t i = 0; i < trimmed.length(); i++) {
        if (trimmed[i] < '0' || trimmed[i] > '9') {
            isNumeric = false;
            break;
        }
    }
    
    if (isNumeric && trimmed.length() > 0) {
        int index = trimmed.toInt();
        
        // Index 0 is root
        if (index == 0) {
            return _rootNumber;
        }
        
        // Indices 1-5 map to slots 0-4
        if (index >= 1 && index <= MAX_ADDITIONAL_NUMBERS) {
            int slot = index - 1;
            String storedNumber = _prefs.getString(getPhoneKey(slot).c_str(), "");
            if (storedNumber.length() > 0) {
                return storedNumber;
            }
        }
        
        return "";  // Index out of range or empty slot
    }
    
    // Otherwise, treat as phone number
    String normalized = normalizeNumber(trimmed);
    
    // Check if it's root
    if (normalized == _rootNumber) {
        return _rootNumber;
    }
    
    // Check if it's in stored numbers
    int slot = findPhoneSlot(normalized);
    if (slot >= 0) {
        return normalized;
    }
    
    return "";  // Not found
}

bool PhoneNumberManager::isMuted(const String &number) {
    String normalized = normalizeNumber(number);

    // Check if it's root
    if (normalized == _rootNumber) {
        return _prefs.getBool("mute_root", false);
    }

    int slot = findPhoneSlot(normalized);
    if (slot >= 0) {
        return _prefs.getBool(getMuteKey(slot).c_str(), false);
    }

    return false;
}

bool PhoneNumberManager::muteNumber(const String &numberOrIndex) {
    String resolved = resolvePhoneByIndexOrNumber(numberOrIndex);
    if (resolved.length() == 0) {
        log_w("[PhoneNumberManager] Number not found to mute: %s", numberOrIndex.c_str());
        return false;
    }

    // Root number: store mute state in special preference
    if (resolved == _rootNumber) {
        _prefs.putBool("mute_root", true);
        log_i("[PhoneNumberManager] Muted root number %s", resolved.c_str());
        return true;
    }

    int slot = findPhoneSlot(resolved);
    if (slot >= 0) {
        _prefs.putBool(getMuteKey(slot).c_str(), true);
        log_i("[PhoneNumberManager] Muted %s", resolved.c_str());
        return true;
    }

    return false;
}

bool PhoneNumberManager::unmuteNumber(const String &numberOrIndex) {
    String resolved = resolvePhoneByIndexOrNumber(numberOrIndex);
    if (resolved.length() == 0) {
        log_w("[PhoneNumberManager] Number not found to unmute: %s", numberOrIndex.c_str());
        return false;
    }

    // Root number: store unmute state in special preference
    if (resolved == _rootNumber) {
        _prefs.putBool("mute_root", false);
        log_i("[PhoneNumberManager] Unmuted root number %s", resolved.c_str());
        return true;
    }

    int slot = findPhoneSlot(resolved);
    if (slot >= 0) {
        _prefs.putBool(getMuteKey(slot).c_str(), false);
        log_i("[PhoneNumberManager] Unmuted %s", resolved.c_str());
        return true;
    }

    return false;
}

String PhoneNumberManager::getMuteStatusList() {
    String result = "Mute status:\n";
    
    // Root number
    bool rootMuted = _prefs.getBool("mute_root", false);
    result += "[0] " + _rootNumber + " (" + (rootMuted ? "Muted" : "Active") + ")\n";

    // Additional numbers
    for (int i = 0; i < MAX_ADDITIONAL_NUMBERS; i++) {
        String storedNumber = _prefs.getString(getPhoneKey(i).c_str(), "");
        if (storedNumber.length() > 0) {
            bool muted = _prefs.getBool(getMuteKey(i).c_str(), false);
            result += "[" + String(i + 1) + "] " + storedNumber + " (" + (muted ? "Muted" : "Active") + ")\n";
        }
    }

    return result;
}