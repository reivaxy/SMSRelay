#include "MainPowerCheck.h"

// GPIO36 maps to ADC1_CHANNEL_0 on ESP32
#define MAIN_POWER_ADC_PIN 36  

MainPowerCheck::MainPowerCheck(SMSSender &sender, const String &targetNumber)
    : _sender(sender), _targetNumber(targetNumber) 
{
    // Configure the ADC pin
#ifdef MAIN_POWER_ADC_PIN
    pinMode(MAIN_POWER_ADC_PIN, INPUT);
#endif
}

void MainPowerCheck::check()
{
    int adcValue = readGPIO00ADC();
    
    // Log to Serial every 500ms
    // if (millis() - _lastSerialLog >= 500) {
    //     _lastSerialLog = millis();
    //     log_d("[MainPowerCheck] MainAdcPin ADC Level: %d\n", adcValue);
    // }
    

    // Check if level has crossed below threshold
    if (adcValue < POWER_ADC_THRESHOLD && !_lowPowerAlertSent) {
        log_i("Main power low (MainAdcPin=%d), sending SMS alert...", adcValue);
        String alertMsg = "ALERT: Main power level is low (MainAdcPin=" + String(adcValue) + ")";
        if (_sender.send(_targetNumber, alertMsg)) {
            log_i("[OK] Low power alert SMS sent successfully");
            _lowPowerAlertSent = true;
            _normalPowerAlertSent = false;
            _lastADCValue = adcValue;
        } else {
            log_i("[ERROR] Failed to send low power alert SMS");
        }
    }
    // Check if level has crossed back above threshold
    else if (adcValue >= POWER_ADC_THRESHOLD && _lowPowerAlertSent && !_normalPowerAlertSent) {
        log_i("Main power restored (MainAdcPin=%d), sending SMS notification...", adcValue);
        String restoreMsg = "NOTIFICATION: Main power level restored (MainAdcPin=" + String(adcValue) + ")";
        if (_sender.send(_targetNumber, restoreMsg)) {
            log_i("[OK] Power restored SMS sent successfully");
            _normalPowerAlertSent = true;
            _lowPowerAlertSent = false;
        } else {
            log_i("[ERROR] Failed to send power restored SMS");
        }
    }
    
    _lastADCValue = adcValue;

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
