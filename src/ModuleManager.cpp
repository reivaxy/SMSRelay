#include "utilities.h"  // Defines the GSM modem model required by TinyGsmClient.h (via ClockManager.h)
#include "ModuleManager.h"
#include "ClockManager.h"
#include "AlertManager.h"
#include <ArduinoJson.h>
#include <ctime>

static String htmlEscape(const String &s) {
    String out = "";
    for (int i = 0; i < (int)s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

static String formatTimestamp(unsigned long epoch) {
    if (epoch == 0) {
        return "-";
    }
    time_t t = (time_t)epoch;
    struct tm *timeinfo = gmtime(&t);  // Epoch already includes local offsets
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buffer);
}

ModuleManager::ModuleManager(ClockManager &clockManager, AlertManager &alertManager)
    : _clockManager(clockManager), _alertManager(alertManager), _lastSweep(0), _sweeping(false), _sweepIndex(0) {
    for (size_t i = 0; i < MAX_MODULES; i++) {
        _modules[i].used = false;
        _modules[i].failed = false;
        _modules[i].alert = false;
        _modules[i].name[0] = '\0';
        _modules[i].type[0] = '\0';
        _modules[i].timestamp = 0;
    }
}

bool ModuleManager::registerModule(const IPAddress &ip, const String &name, const String &type) {
    int freeSlot = -1;
    unsigned long now = _clockManager.isInitialized() ? _clockManager.getCurrentTime() : 0;
    for (size_t i = 0; i < MAX_MODULES; i++) {
        if (_modules[i].used && _modules[i].ip == ip) {
            strlcpy(_modules[i].name, name.c_str(), sizeof(_modules[i].name));
            strlcpy(_modules[i].type, type.c_str(), sizeof(_modules[i].type));
            _modules[i].timestamp = now;
            _modules[i].failed = false;  // A ping proves the module is alive; re-arms the outage alert
            return true;
        }
        if (!_modules[i].used && freeSlot < 0) {
            freeSlot = (int)i;
        }
    }
    if (freeSlot < 0) {
        log_w("[MODULES] Module table full, ignoring %s", ip.toString().c_str());
        return false;
    }
    _modules[freeSlot].used = true;
    _modules[freeSlot].failed = false;
    _modules[freeSlot].alert = false;
    _modules[freeSlot].ip = ip;
    strlcpy(_modules[freeSlot].name, name.c_str(), sizeof(_modules[freeSlot].name));
    strlcpy(_modules[freeSlot].type, type.c_str(), sizeof(_modules[freeSlot].type));
    _modules[freeSlot].timestamp = now;
    return true;
}

bool ModuleManager::removeModule(const IPAddress &ip) {
    for (size_t i = 0; i < MAX_MODULES; i++) {
        if (_modules[i].used && _modules[i].ip == ip) {
            _modules[i].used = false;
            _modules[i].failed = false;
            _modules[i].alert = false;
            _modules[i].name[0] = '\0';
            _modules[i].type[0] = '\0';
            _modules[i].timestamp = 0;
            return true;
        }
    }
    return false;
}

bool ModuleManager::isKnownModule(const IPAddress &ip) const {
    for (size_t i = 0; i < MAX_MODULES; i++) {
        if (_modules[i].used && _modules[i].ip == ip) {
            return true;
        }
    }
    return false;
}

int ModuleManager::fetchModuleStatus(const IPAddress &ip, String &body) {
    // Plain WiFiClient request (HTTPClient.h pulls mbedtls headers that clash with the -DX build flag)
    const int maxResponseSize = 4096;
    WiFiClient conn;
    if (!conn.connect(ip, 80, 2000)) {
        return -1;
    }

    conn.print("GET /api/status HTTP/1.1\r\nHost: " + ip.toString() + "\r\nConnection: close\r\n\r\n");

    String response = "";
    unsigned long deadline = millis() + 3000;
    while ((conn.connected() || conn.available()) && millis() < deadline && (int)response.length() < maxResponseSize) {
        if (conn.available()) {
            response += (char)conn.read();
        } else {
            delay(1);
        }
    }
    conn.stop();

    // Status line: "HTTP/1.1 200 OK"
    int firstSpace = response.indexOf(' ');
    int headersEnd = response.indexOf("\r\n\r\n");
    if (firstSpace < 0 || headersEnd < 0) {
        return -2;
    }
    int code = response.substring(firstSpace + 1, firstSpace + 4).toInt();
    body = response.substring(headersEnd + 4);

    // The "module" object carries a boolean "alert" flag shown in the module list
    if (code == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, body)) {
            bool alert = doc["module"]["alert"] | false;
            for (size_t i = 0; i < MAX_MODULES; i++) {
                if (_modules[i].used && _modules[i].ip == ip) {
                    _modules[i].alert = alert;
                    break;
                }
            }
        }
    }
    return code;
}

int ModuleManager::fetchModuleStatusByNumber(int number, String &body) {
    if (number <= 0) {
        return -3;
    }

    int moduleNumber = 0;
    for (size_t i = 0; i < MAX_MODULES; i++) {
        if (!_modules[i].used) continue;
        if (++moduleNumber == number) {
            return fetchModuleStatus(_modules[i].ip, body);
        }
    }
    return -3;
}

void ModuleManager::check() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (!_sweeping) {
        if (millis() - _lastSweep < CHECK_INTERVAL_MS) {
            return;
        }
        _sweeping = true;
        _sweepIndex = 0;
        _lastSweep = millis();
        log_i("[MODULES] Starting module status check loop");
    }

    // Next used slot of the current sweep
    while (_sweepIndex < MAX_MODULES && !_modules[_sweepIndex].used) {
        _sweepIndex++;
    }
    if (_sweepIndex >= MAX_MODULES) {
        _sweeping = false;
        return;
    }

    Module &mod = _modules[_sweepIndex++];
    String body;
    log_i("[MODULES] Checking %s (%s)", mod.name, mod.ip.toString().c_str());
    int code = fetchModuleStatus(mod.ip, body);
    log_i("[MODULES] %s (%s) check result: code %d", mod.name, mod.ip.toString().c_str(), code);

    if (code == 200) {
        mod.failed = false;
        JsonDocument doc;
        if (!deserializeJson(doc, body)) {
            const char *name = doc["name"] | "";
            if (name[0] != '\0' && strncmp(mod.name, name, sizeof(mod.name) - 1) != 0) {
                log_i("[MODULES] %s: name updated '%s' -> '%s'", mod.ip.toString().c_str(), mod.name, name);
                strlcpy(mod.name, name, sizeof(mod.name));
            }
        }
        return;
    }

    log_w("[MODULES] %s (%s) status check failed (code %d)", mod.name, mod.ip.toString().c_str(), code);
    if (!mod.failed) {
        mod.failed = true;  // One alert per outage; re-armed after a successful check
        String label = (mod.name[0] != '\0') ? String(mod.name) : mod.ip.toString();
        String url = "http://" + mod.ip.toString() + "/";
        _alertManager.sendAlert("Module " + label + " not responding",
                                "Module " + label + " does not answer on " + url,
                                url);
    }
}

String ModuleManager::generateModulesHTML() const {
    String rows = "";
    for (size_t i = 0; i < MAX_MODULES; i++) {
        if (!_modules[i].used) continue;
        String ip = _modules[i].ip.toString();
        rows += _modules[i].failed ? "                <tr class=\"down\">" : "                <tr>";
        rows += "<td>" + htmlEscape(String(_modules[i].name));
        if (_modules[i].failed) {
            rows += " <span class=\"tag tag-high\">NO RESPONSE</span>";
        }
        if (_modules[i].alert) {
            rows += " <span class=\"tag tag-high alert-tag\">ALERT</span>";
        }
        if (_modules[i].type[0] != '\0') {
            rows += "<br><span class=\"dev-type\">(" + htmlEscape(String(_modules[i].type)) + ")</span>";
        }
        rows += "</td>";
        String lastPing = formatTimestamp(_modules[i].timestamp);
        lastPing.replace(" ", "<br>");  // date on the first line, time on the second
        rows += "<td>" + lastPing + "</td>";
        rows += "<td><a class=\"btn btn-sm\" href=\"http://" + ip + "/\" target=\"_blank\" rel=\"noopener\">home</a></td>";
        rows += "<td><button type=\"button\" class=\"btn-sm status-btn\" data-ip=\"" + ip + "\">status</button></td>";
        rows += "<td><button type=\"button\" class=\"btn-sm btn-danger remove-btn\" data-ip=\"" + ip + "\" data-name=\"" + htmlEscape(String(_modules[i].name)) + "\">X</button></td>";
        rows += "</tr>\n";
    }
    if (rows.length() == 0) {
        return "            <div class=\"empty\">No devices recorded</div>\n";
    }
    String html = "            <table class=\"devices\">\n";
    html += "                <tr><th>Name</th><th>Last ping</th><th>Home</th><th>Status</th><th>Remove</th></tr>\n";
    html += rows;
    html += "            </table>\n";
    html += "            <dialog id=\"statusDialog\">\n";
    html += "                <pre id=\"statusBody\"></pre>\n";
    html += "                <button type=\"button\" id=\"statusQuit\">Quit</button>\n";
    html += "            </dialog>\n";
    return html;
}

String ModuleManager::getModuleList() const {
    String list = "";
    int number = 0;
    for (size_t i = 0; i < MAX_MODULES; i++) {
        if (!_modules[i].used) continue;
        list += "\n" + String(++number) + ". " + String(_modules[i].name);
    }
    return number == 0 ? "No modules recorded" : "Modules:" + list;
}
