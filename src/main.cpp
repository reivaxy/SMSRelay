/**
 * @file      SendSMS.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2023-11-16
 * @note      SIM7670G - SIM7670G-MNGV 2374B04 version supports SMS function,
 *            but it requires the operator base station to support SMS Over SGS service to send, otherwise it will be invalid
 *            `A7670E-LNXY-UBL` this version does not support voice and SMS functions.
 */

#include <Arduino.h>
#include "utilities.h"
#include "secret.h"

#ifdef LILYGO_SIM7000G
#warning "SIM7000G SMS function, the network access mode must be GSM, not NB-IOT"
#endif

#ifdef TINY_GSM_MODEM_SIM7080
#error "This modem not sms function"
#endif

#include "Modem.h"
#include "SMSSender.h"
#include "SMSReader.h"
#include "SMSForwarder.h"
#include "SMSProcessor.h"
#include "BatteryProcessor.h"
#include "MainPowerCheck.h"
#include "SerialConsole.h"
#include "TemperatureHumidityProcessor.h"
#include "ConfigManager.h"
#include "PhoneNumberManager.h"
#include "ClockManager.h"
#include "AlertManager.h"
#include "OTAManager.h"
#include "WebManager.h"

Modem                        modem;
ConfigManager                configManager;
SMSSender                    sender(modem.getModem(), modem.getSerialStream(), configManager);
SMSReader                    reader(modem.getModem(), modem.getSerialStream());
SMSForwarder                 forwarder(sender, ROOT_NUMBER);
PhoneNumberManager           phoneNumberManager(ROOT_NUMBER);
ClockManager                 clockManager(modem.getModem(), configManager);
AlertManager                 alertManager(sender, configManager, phoneNumberManager, clockManager);
BatteryProcessor             batteryProcessor(sender, ROOT_NUMBER, configManager, phoneNumberManager);
MainPowerCheck               mainPowerCheck(sender, ROOT_NUMBER, configManager, phoneNumberManager, alertManager);
TemperatureHumidityProcessor tempHumidityProcessor(sender, ROOT_NUMBER, BOARD_DHT_PIN, configManager, phoneNumberManager, alertManager);
OTAManager                   otaManager(sender, ROOT_NUMBER, configManager, phoneNumberManager, alertManager, mainPowerCheck, modem);
WebManager                   webManager(configManager, sender, phoneNumberManager, alertManager, modem);
SMSProcessor                 processor(sender, ROOT_NUMBER, reader, mainPowerCheck, batteryProcessor, tempHumidityProcessor, configManager, phoneNumberManager, alertManager, clockManager, otaManager, webManager, modem);
SerialConsole                console(processor);

void setup()
{
    Serial.begin(115200);

    // Initialize configuration manager for persistent storage
    configManager.init();

    // Initialize phone number manager for authorized numbers
    phoneNumberManager.init();
    
    // Initialize temperature and humidity sensor
    tempHumidityProcessor.init();

    // Initialize modem hardware, serial communication, network, and SMS configuration
    if (!modem.init()) {
        log_e("Modem initialization failed, will retry in main loop");
    }
}




void loop()
{
    // Handle deferred initialization after modem is ready
    static bool deferredInitDone = false;
    if (!deferredInitDone && modem.isInitialized()) {
        deferredInitDone = true;
        
        // Send power-on notification to all authorized numbers
        log_i("Sending power-on notification...");
        String powerOnMsg = "Device powered on" 
        #ifdef GIT_REV
            + String(" (Rev ") + String(GIT_REV) + String(")");
        #endif
        
        for (const auto &entry : phoneNumberManager.getAllNumbers()) {
            if (sender.send(entry.number, powerOnMsg)) {
                log_i("[OK] Power-on SMS sent to %s", entry.number.c_str());
            } else {
                log_i("[ERROR] Failed to send power-on SMS to %s", entry.number.c_str());
            }
        }
    }

    // Retry clock initialization as long as it's failing
    static bool clockInitDone = false;
    static unsigned long lastClockInitAttempt = 0;
    const unsigned long CLOCK_INIT_RETRY_INTERVAL = 5000;  // Retry every 5 seconds
    
    if (!clockInitDone && modem.isInitialized() && millis() - lastClockInitAttempt >= CLOCK_INIT_RETRY_INTERVAL) {
        lastClockInitAttempt = millis();
        log_i("Attempting clock initialization from network time...");
        if (clockManager.init()) {
            clockInitDone = true;
            log_i("Clock initialized successfully");
        } else {
            log_w("Failed to initialize clock from network, will retry");
        }
    }

    // Check for Serial console commands
    console.check();

    // Check modem connection periodically and reconnect if needed
    modem.checkConnection();

    // Check clock synchronization with network (every 2 hours)
    clockManager.check();

    // Check for incoming SMS periodically (but only if modem is connected)
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 2000) {
        lastCheck = millis();
        if (modem.isConnected()) {
            reader.check(ROOT_NUMBER, processor, forwarder, phoneNumberManager);
        }
    }

    // Check battery level
    batteryProcessor.check();

    // Check main power level
    mainPowerCheck.check();

    // Check temperature and humidity levels
    tempHumidityProcessor.check();

    // Check pending alerts and resend if needed
    alertManager.check();

    // Check OTA web server if active
    otaManager.check();

    // Check web configuration server if active
    webManager.check();

    delay(100);
}

#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGO-T-A76XX/tree/main/lib) to the arduino libraries directory , See README"
#endif
