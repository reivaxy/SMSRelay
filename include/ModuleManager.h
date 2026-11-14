#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// Forward declarations
class ClockManager;
class AlertManager;

// Registry of the modules (remote devices) that ping this server.
// Every CHECK_INTERVAL_MS, only stale modules are asked for /api/status;
// an alert is raised (once per outage) when a module does not answer.
class ModuleManager {
public:
    static constexpr size_t MAX_MODULES = 10;
    static constexpr size_t NAME_LEN = 11;
    static constexpr size_t TYPE_LEN = 11;
    static constexpr unsigned long CHECK_INTERVAL_MS = 60UL * 1000UL;
    static constexpr unsigned long STALE_AFTER_MS = 330UL * 1000UL;
    static constexpr unsigned long DISCONNECTED_RETRY_MS = 5UL * 60UL * 1000UL;

    struct Module {
        IPAddress ip;
        char      mac[18];         // Stable identity, uppercase colon-separated MAC
        char      name[NAME_LEN + 1];
        char      type[TYPE_LEN + 1];
        unsigned long timestamp;  // Local epoch time of last update (0 if clock not set)
        unsigned long lastUpdateMs; // Monotonic time for age checks, independent of clock sync
        unsigned long lastCheckMs;  // Last attempted status check; limits disconnected retries
        bool      used;
        bool      failed;         // Last status check failed (alert already raised)
        bool      alert;          // Last status reported module.alert == true
        String    reportedAlerts; // Last reported alert messages, cleared on recovery
    };

    ModuleManager(ClockManager &clockManager, AlertManager &alertManager);

    // Canonical MAC address, or an empty string if invalid
    static String normalizeMac(const String &mac);

    // Update an existing MAC's IP/status, or add a new module; false if invalid/full
    bool registerModule(const IPAddress &ip, const String &mac, const String &name, const String &type, bool alert);

    // Report new/changed alerts from either a ping or a fetched status payload
    void reportModuleAlerts(const String &mac, JsonObjectConst status);

    // Remove a module by its stable MAC identity
    bool removeModule(const String &mac);

    // Remove all modules from the table
    void clear();

    // True if the IP is in the module table
    bool isKnownModule(const IPAddress &ip) const;

    // GET status and refresh the matching entry; <=0 on transport/invalid-status errors
    int fetchModuleStatus(const IPAddress &ip, String &body);

    // Fetch status for a module using its one-based getModuleList() number; returns -3 if absent
    int fetchModuleStatusByNumber(int number, String &body);

    // Generate HTML for the list of modules
    String generateModulesHTML() const;

    // Generate a numbered list of module names
    String getModuleList() const;

    // Call from loop(): every CHECK_INTERVAL_MS, request status only for entries older
    // than STALE_AFTER_MS (one per call), and alert when a stale module does not answer
    void check();

private:
    ClockManager &_clockManager;
    AlertManager &_alertManager;
    Module        _modules[MAX_MODULES];
    unsigned long _lastSweep;
    bool          _sweeping;
    size_t        _sweepIndex;
};
