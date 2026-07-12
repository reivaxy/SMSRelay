#include "AlertManager.h"
#include "ConfigManager.h"
#include "utilities.h"
#include <random>

AlertManager::AlertManager(SMSSender &sender, ConfigManager &configManager, PhoneNumberManager &phoneNumberManager)
    : _sender(sender), _configManager(configManager), _phoneNumberManager(phoneNumberManager), _lastCheck(0) {
    log_i("[ALERT] AlertManager initialized");
}

AlertManager::~AlertManager() {
    _pendingAlerts.clear();
}

String AlertManager::generateUniqueCode() {
    // Generate a 3-digit random code (000-999) that doesn't already exist
    String code;
    bool unique = false;
    int attempts = 0;
    
    while (!unique && attempts < 100) {
        // Use Arduino's random function for better randomness
        int num = random(1000);  // 0-999
        code = String(num);
        
        // Pad with zeros to ensure 3 digits
        while (code.length() < 3) {
            code = "0" + code;
        }
        
        // Check if code already exists
        if (findAlertByCode(code) == -1) {
            unique = true;
            log_d("[ALERT] Generated unique code: %s (attempt %d)", code.c_str(), attempts + 1);
        }
        attempts++;
    }
    
    if (!unique) {
        log_e("[ALERT] Failed to generate unique code after 100 attempts!");
    }
    
    return code;
}

String AlertManager::sendAlert(const String &title, const String &message, const String &cause, AlertLevel level) {
    String code = generateUniqueCode();
    
    Alert newAlert;
    newAlert.code = code;
    newAlert.title = title;
    newAlert.message = message;
    newAlert.cause = cause;
    newAlert.created = millis();
    newAlert.lastSent = 0;  // Will be sent immediately
    newAlert.level = level;
    
    _pendingAlerts.push_back(newAlert);
    
    // Send immediately to all non-muted numbers
    sendAlertToAll(_pendingAlerts.back());
    
    log_i("[ALERT] New alert created: code=%s, title=%s, cause=%s", code.c_str(), title.c_str(), cause.c_str());
    
    return code;
}

void AlertManager::sendAlertToAll(Alert &alert) {
    unsigned long now = millis();
    alert.lastSent = now;
    
    String message = "[" + alert.code + "] " + alert.title + "\n" + alert.message + 
                    "\nReply: ACK " + alert.code;
    
    auto allNumbers = _phoneNumberManager.getAllNumbers();
    int sentCount = 0;
    int skippedCount = 0;
    
    for (const auto &entry : allNumbers) {
        // Check if muted (but still allow ACK from muted numbers)
        if (_phoneNumberManager.isMuted(entry.number)) {
            skippedCount++;
            log_d("[ALERT] Skipping alert %s to muted number %s", alert.code.c_str(), entry.number.c_str());
            continue;
        }
        
        if (_sender.send(entry.number, message)) {
            sentCount++;
            log_i("[ALERT] Alert %s sent to %s", alert.code.c_str(), entry.number.c_str());
        } else {
            log_w("[ALERT] Failed to send alert %s to %s", alert.code.c_str(), entry.number.c_str());
        }
    }
    
    if (sentCount > 0) {
        log_i("[ALERT] Alert %s sent to %d number(s), skipped %d muted", alert.code.c_str(), sentCount, skippedCount);
    }
}

bool AlertManager::handleACK(const String &senderNumber, const String &code) {
    int index = findAlertByCode(code);
    
    if (index == -1) {
        log_w("[ALERT] ACK received for unknown alert code: %s from %s", code.c_str(), senderNumber.c_str());
        return false;
    }
    
    Alert &alert = _pendingAlerts[index];
    
    // Check if already ACKed by this number
    for (const auto &ackedNum : alert.ackedBy) {
        if (ackedNum == senderNumber) {
            log_d("[ALERT] Duplicate ACK from %s for alert %s", senderNumber.c_str(), code.c_str());
            return true;  // Already handled
        }
    }
    
    // Add this number to the ACKed list
    alert.ackedBy.push_back(senderNumber);
    log_i("[ALERT] Alert %s ACKed by %s", code.c_str(), senderNumber.c_str());
    
    // If this is the first ACK, notify others
    if (alert.ackedBy.size() == 1) {
        notifyACKToOthers(alert, senderNumber);
    }
    
    return true;
}

void AlertManager::notifyACKToOthers(const Alert &alert, const String &ackSenderNumber) {
    // Get the alias or number of who ACKed
    String ackerAlias = _phoneNumberManager.getAlias(ackSenderNumber);
    if (ackerAlias.isEmpty()) {
        ackerAlias = ackSenderNumber;
    }
    
    String notifyMessage = "Alert [" + alert.code + "] ACKed by: " + ackerAlias;
    
    auto allNumbers = _phoneNumberManager.getAllNumbers();
    int sentCount = 0;
    
    for (const auto &entry : allNumbers) {
        // Don't send to the number that sent the ACK
        if (entry.number == ackSenderNumber) {
            continue;
        }
        
        // Don't send to muted numbers
        if (_phoneNumberManager.isMuted(entry.number)) {
            log_d("[ALERT] Skipping ACK notification to muted number %s", entry.number.c_str());
            continue;
        }
        
        if (_sender.send(entry.number, notifyMessage)) {
            sentCount++;
            log_i("[ALERT] ACK notification sent to %s", entry.number.c_str());
        } else {
            log_w("[ALERT] Failed to send ACK notification to %s", entry.number.c_str());
        }
    }
    
    log_i("[ALERT] ACK notification for alert %s sent to %d number(s)", alert.code.c_str(), sentCount);
}

int AlertManager::findAlertByCode(const String &code) {
    for (size_t i = 0; i < _pendingAlerts.size(); i++) {
        if (_pendingAlerts[i].code == code) {
            return i;
        }
    }
    return -1;
}

void AlertManager::check() {
    unsigned long now = millis();
    
    // Check every 10 seconds for alerts that need resending
    if (now - _lastCheck < 10000) {
        return;
    }
    _lastCheck = now;
    
    // Get resend delay from config (in minutes, convert to milliseconds)
    int delayMinutes = _configManager.getInt(ConfigManager::Param::ALERT_RESEND_DELAY_MINS);
    unsigned long resendIntervalMs = (unsigned long)delayMinutes * 60000;
    log_d("[ALERT] Check cycle: delay config = %d minutes (%lu ms)", delayMinutes, resendIntervalMs);
    
    if (_pendingAlerts.empty()) {
        log_d("[ALERT] No pending alerts to check");
        return;
    }
    
    for (auto &alert : _pendingAlerts) {
        unsigned long timeSinceLastSent = now - alert.lastSent;
        
        // Skip resending if already ACKed by anyone
        if (!alert.ackedBy.empty()) {
            log_d("[ALERT] Alert %s: Already ACKed by %d user(s), skipping resend", 
                  alert.code.c_str(), (int)alert.ackedBy.size());
            continue;
        }
        
        log_d("[ALERT] Alert %s: time since last sent = %lu ms, interval = %lu ms", 
              alert.code.c_str(), timeSinceLastSent, resendIntervalMs);
        
        // Check if enough time has passed since last send
        if (timeSinceLastSent >= resendIntervalMs) {
            log_i("[ALERT] Resending alert %s (last sent %lu ms ago)", 
                  alert.code.c_str(), timeSinceLastSent);
            sendAlertToAll(alert);
        }
    }
}

void AlertManager::clearAll() {
    log_w("[ALERT] Clearing all pending alerts");
    _pendingAlerts.clear();
}

String AlertManager::getPendingAlertsList() {
    if (_pendingAlerts.empty()) {
        return "No pending alerts";
    }
    
    String msg = "Pending alerts:\n";
    for (size_t i = 0; i < _pendingAlerts.size(); i++) {
        const auto &alert = _pendingAlerts[i];
        msg += "[" + alert.code + "] " + alert.title;
        if (!alert.cause.isEmpty()) {
            msg += " (" + alert.cause + ")";
        }
        if (!alert.ackedBy.empty()) {
            msg += " [ACK:" + String((int)alert.ackedBy.size()) + "]";
        }
        msg += "\n";
    }
    
    return msg;
}
