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

Modem            modem;
SMSSender        sender(modem.getModem(), modem.getSerialStream());
SMSReader        reader(modem.getModem(), modem.getSerialStream());
SMSForwarder     forwarder(sender, SMS_TARGET);
BatteryProcessor batteryProcessor(sender, SMS_TARGET);
MainPowerCheck   mainPowerCheck(sender, SMS_TARGET);
SMSProcessor     processor(sender, SMS_TARGET, reader, mainPowerCheck);
SerialConsole    console(reader, forwarder, batteryProcessor, mainPowerCheck);

void setup()
{
    Serial.begin(115200);

    // Initialize modem hardware, serial communication, network, and SMS configuration
    if (!modem.init()) {
        log_e("Modem initialization failed");
        return;
    }

    // Send power-on notification
    log_i("Sending power-on notification...");
    String powerOnMsg = "Device powered on";

    if (modem.getModem().sendSMS(SMS_TARGET, powerOnMsg)) {
        log_i("[OK] Power-on SMS sent successfully");
    } else {
        log_i("[ERROR] Failed to send power-on SMS");
    }
}




void loop()
{
    // Check for Serial console commands
    console.check();

    // Check for incoming SMS periodically
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 2000) {
        lastCheck = millis();
        reader.check(SMS_TARGET, processor, forwarder);
    }

    // Check battery level
    batteryProcessor.check();

    // Check main power level
    mainPowerCheck.check();

    delay(100);
}

#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGO-T-A76XX/tree/main/lib) to the arduino libraries directory , See README"
#endif
