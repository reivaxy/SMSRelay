#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Forward declarations
class ClockManager;
class AlertManager;

// Registry of the modules (remote devices) that ping this server.
// Every CHECK_INTERVAL_MS each module's /api/status is requested; an alert is raised
// (once per outage) when a module does not answer.
class ModuleManager {
public:
    static constexpr size_t MAX_MODULES = 10;
    static constexpr size_t NAME_LEN = 11;
    static constexpr size_t TYPE_LEN = 11;
    static constexpr unsigned long CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL;

    struct Module {
        IPAddress ip;
        char      name[NAME_LEN + 1];
        char      type[TYPE_LEN + 1];
        unsigned long timestamp;  // Local epoch time of last ping (0 if clock not set)
        bool      used;
        bool      failed;         // Last status check failed (alert already raised)
        bool      alert;          // Last status reported module.alert == true
    };

    ModuleManager(ClockManager &clockManager, AlertManager &alertManager);

    // Store the module IP, name and type, or update name/type/timestamp if the IP is already known
    // Returns false if the table is full and the IP is new
    bool registerModule(const IPAddress &ip, const String &name, const String &type);

    // Remove a module from the table, returns false if the IP is unknown
    bool removeModule(const IPAddress &ip);

    // Remove all modules from the table
    void clear();

    // True if the IP is in the module table
    bool isKnownModule(const IPAddress &ip) const;

    // GET http://<ip>/api/status on a module; returns HTTP code (<=0 on connection error)
    int fetchModuleStatus(const IPAddress &ip, String &body);

    // Fetch status for a module using its one-based getModuleList() number; returns -3 if absent
    int fetchModuleStatusByNumber(int number, String &body);

    // Generate HTML for the list of modules
    String generateModulesHTML() const;

    // Generate a numbered list of module names
    String getModuleList() const;

    // Call from loop(): every CHECK_INTERVAL_MS, requests /api/status from each module
    // (one module per call, to keep each call short) and alerts when one does not answer
    void check();

private:
    ClockManager &_clockManager;
    AlertManager &_alertManager;
    Module        _modules[MAX_MODULES];
    unsigned long _lastSweep;
    bool          _sweeping;
    size_t        _sweepIndex;
};
