#include "BatteryProcessor.h"
#include "ConfigManager.h"

BatteryProcessor::BatteryProcessor(SMSSender &sender, const String &targetNumber, ConfigManager &configManager)
    : _sender(sender), _targetNumber(targetNumber), _configManager(configManager) {}

void BatteryProcessor::check()
{
    if (millis() - _lastCheck < 10000) return;
    _lastCheck = millis();

    // Get current thresholds from manager
    int batAdcThreshold = _configManager.getInt(ConfigManager::Param::BAT_ADC_THRESHOLD);
    int batAdcNearEmpty = _configManager.getInt(ConfigManager::Param::BAT_ADC_NEAR_EMPTY);

    int adcValue = readBatADC();
    if (adcValue > 0 && adcValue < batAdcThreshold) {
        if (adcValue < batAdcNearEmpty && !_nearEmptyAlertSent) {
            log_i("Battery near empty (BatteryAdcPin=%d), sending SMS...", adcValue);
            if (_sender.send(_targetNumber, "Battery near empty (BatteryAdcPin=" + String(adcValue) + ")")) {
                log_i("[OK] Battery near empty SMS sent successfully");
                _nearEmptyAlertSent = true;
                _lastAlertADC = adcValue;
            } else {
                log_i("[ERROR] Failed to send battery near empty SMS");
            }
        } else if (!_batteryAlertSent) {
            log_i("Battery power detected (BatteryAdcPin=%d), sending SMS...", adcValue);
            if (_sender.send(_targetNumber, "Device is now on battery power (BatteryAdcPin=" + String(adcValue) + ")")) {
                log_i("[OK] Battery alert SMS sent successfully");
                _batteryAlertSent = true;
                _usbAlertSent = false;
                _lastAlertADC = adcValue;
            } else {
                log_i("[ERROR] Failed to send battery alert SMS");
            }
        } else if (_lastAlertADC - adcValue >= 20) {
            // Use this to find out the best value for BAT_ADC_NEAR_EMPTY_THRESHOLD
            // log_i("Battery level dropped (ADC=%d, last alert ADC=%d), sending SMS...", adcValue, _lastAlertADC);
            // if (_sender.send(_targetNumber, "Battery level dropped (ADC=" + String(adcValue) + ")")) {
            //     log_i("[OK] Battery drop SMS sent successfully");
            //     _lastAlertADC = adcValue;
            // } else {
            //     log_i("[ERROR] Failed to send battery drop SMS");
            // }
        }
    } else {
        if (!_usbAlertSent && _batteryAlertSent) {
            log_i("Back on USB power, sending SMS...");
            if (_sender.send(_targetNumber, "Device is now on USB power")) {
                log_i("[OK] USB alert SMS sent successfully");
                _usbAlertSent = true;
            } else {
                log_i("[ERROR] Failed to send USB alert SMS");
            }
        }
        _batteryAlertSent   = false;
        _nearEmptyAlertSent = false;
        _lastAlertADC       = 0;
    }
}

int BatteryProcessor::readBatADC()
{
#ifdef BOARD_BAT_ADC_PIN
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += analogRead(BOARD_BAT_ADC_PIN);
        delay(10);
    }
    return sum / 10;
#else
    return 0;
#endif
}

void BatteryProcessor::resetAlertFlags()
{
    log_i("Resetting battery alert flags");
    _batteryAlertSent = false;
    _usbAlertSent = false;
    _nearEmptyAlertSent = false;
    _lastAlertADC = 0;
}
