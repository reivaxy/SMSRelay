#include "MainPowerCheck.h"
#include "ConfigManager.h"
#include "PhoneNumberManager.h"

// GPIO36 maps to ADC1_CHANNEL_0 on ESP32
#define MAIN_POWER_ADC_PIN 36  

MainPowerCheck::MainPowerCheck(SMSSender &sender, const String &targetNumber, ConfigManager &configManager, PhoneNumberManager &phoneNumberManager)
    : _sender(sender), _targetNumber(targetNumber), _configManager(configManager), _phoneNumberManager(phoneNumberManager)
{
    // Configure the ADC pin
#ifdef MAIN_POWER_ADC_PIN
    pinMode(MAIN_POWER_ADC_PIN, INPUT);
#endif
}

void MainPowerCheck::check()
{
    // Get current threshold from manager
    int powerAdcThreshold = _configManager.getInt(ConfigManager::Param::POWER_ADC_THRESHOLD);
    
    int adcValue = readGPIO00ADC();
    
    // Log to Serial every 500ms
    // if (millis() - _lastSerialLog >= 500) {
    //     _lastSerialLog = millis();
    //     log_d("[MainPowerCheck] MainAdcPin ADC Level: %d\n", adcValue);
    // }
    

    // Check if level has crossed below threshold
    if (adcValue < powerAdcThreshold && !_lowPowerAlertSent) {
        log_i("Main power low (MainAdcPin=%d), sending SMS alert...", adcValue);
        String alertMsg = "ALERT: Main power level is low (MainAdcPin=" + String(adcValue) + ")";
        auto numbers = _phoneNumberManager.getAllNumbers();
        for (const auto &entry : numbers) {
            if (!_phoneNumberManager.isMuted(entry.number)) {
                if (_sender.send(entry.number, alertMsg)) {
                    log_i("[OK] Low power alert SMS sent to %s", entry.number.c_str());
                } else {
                    log_i("[ERROR] Failed to send low power alert SMS to %s", entry.number.c_str());
                }
            } else {
                log_i("[MUTED] Low power alert SMS not sent to %s (muted)", entry.number.c_str());
            }
        }
        _lowPowerAlertSent = true;
        _normalPowerAlertSent = false;
    }
    // Check if level has crossed back above threshold
    else if (adcValue >= powerAdcThreshold && _lowPowerAlertSent && !_normalPowerAlertSent) {
        log_i("Main power restored (MainAdcPin=%d), sending SMS notification...", adcValue);
        String restoreMsg = "NOTIFICATION: Main power level restored (MainAdcPin=" + String(adcValue) + ")";
        auto numbers = _phoneNumberManager.getAllNumbers();
        for (const auto &entry : numbers) {
            if (!_phoneNumberManager.isMuted(entry.number)) {
                if (_sender.send(entry.number, restoreMsg)) {
                    log_i("[OK] Power restored SMS sent to %s", entry.number.c_str());
                } else {
                    log_i("[ERROR] Failed to send power restored SMS to %s", entry.number.c_str());
                }
            } else {
                log_i("[MUTED] Power restored SMS not sent to %s (muted)", entry.number.c_str());
            }
        }
        _normalPowerAlertSent = true;
        _lowPowerAlertSent = false;
    }

}

int MainPowerCheck::readGPIO00ADC()
{
#ifdef MAIN_POWER_ADC_PIN
    // Read and average the ADC value for stability
    int sum = 0;
    const int samples = 4;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(MAIN_POWER_ADC_PIN);
    }
    return sum / samples;
#else
    return 0;  // Return 0 if pin not defined
#endif
}

void MainPowerCheck::resetAlertFlags()
{
    log_i("Resetting main power alert flags");
    _lowPowerAlertSent = false;
    _normalPowerAlertSent = false;
}
