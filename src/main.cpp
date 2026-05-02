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

// Define the serial console for debug prints, if needed
#define TINY_GSM_DEBUG SerialMon

// Set serial for debug console (to the Serial Monitor, default speed 115200)
#define SerialMon Serial

// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS

#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS  // if enabled it requires the streamDebugger lib
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

#include "SMSSender.h"
#include "SMSReader.h"
#include "SMSForwarder.h"
#include "SMSProcessor.h"

SMSSender    sender(modem);
SMSReader    reader(modem, SerialAT);
SMSForwarder forwarder(sender, SMS_TARGET);
SMSProcessor processor(sender, SMS_TARGET);

void setup()
{
    Serial.begin(115200);
#ifdef BOARD_POWERON_PIN
    /* Set Power control pin output
    * * @note      Known issues, ESP32 (V1.2) version of T-A7670, T-A7608,
    *            when using battery power supply mode, BOARD_POWERON_PIN (IO12) must be set to high level after esp32 starts, otherwise a reset will occur.
    * */
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

    // Set modem reset pin ,reset modem
#ifdef MODEM_RESET_PIN
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL); delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL); delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

#ifdef MODEM_FLIGHT_PIN
    // If there is an airplane mode control, you need to exit airplane mode
    pinMode(MODEM_FLIGHT_PIN, OUTPUT);
    digitalWrite(MODEM_FLIGHT_PIN, HIGH);
#endif

#ifdef MODEM_DTR_PIN
    // Pull down DTR to ensure the modem is not in sleep state
    log_i("Set DTR pin %d LOW", MODEM_DTR_PIN);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
#endif

    // Turn on modem
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

#ifdef MODEM_RING_PIN
    // Set ring pin input
    pinMode(MODEM_RING_PIN, INPUT_PULLUP);
#endif

    // Set modem baud
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    log_i("Start modem...");
    delay(3000);

    while (!modem.testAT()) {
        delay(10);
    }


    // Wait PB DONE
    log_i("Wait SMS Done.");
    if (!modem.waitResponse(100000UL, "SMS DONE")) {
        log_i("Can't wait from sms register ....");
        return;
    }


#ifdef NETWORK_APN
    log_i("Set network apn : %s", NETWORK_APN);
    if (!modem.setNetworkAPN(NETWORK_APN)) {
        log_i("Set network apn error !");
    }
#endif


    // Check network registration status and network signal status
    int16_t sq ;
    log_i("Wait for the modem to register with the network.");
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
        status = modem.getRegistrationStatus();
        switch (status) {
        case REG_UNREGISTERED:
        case REG_SEARCHING:
            sq = modem.getSignalQuality();
            log_i("[%lu] Signal Quality:%d", millis() / 1000, sq);
            delay(1000);
            break;
        case REG_DENIED:
            log_i("Network registration was rejected, please check if the APN is correct");
            return ;
        case REG_OK_HOME:
            log_i("Online registration successful");
            break;
        case REG_OK_ROAMING:
            log_i("Network registration successful, currently in roaming mode");
            break;
        default:
            log_i("Registration Status:%d", status);
            delay(1000);
            break;
        }
    }
    log_i("Registration Status:%d", status);
    delay(1000);

    log_i("Init success, ready to receive and forward SMS to %s", SMS_TARGET);
    
    // Enable SMS text mode
    modem.sendAT("+CMGF=1");
    modem.waitResponse();
    
    // Set up to show SMS indication
    modem.sendAT("+CNMI=2,1,0,1");
    modem.waitResponse();

    // Default charset: IRA (ASCII). handleSMS switches to UCS2 for reading.
    modem.sendAT("+CSCS=\"IRA\"");
    modem.waitResponse();
    
    // Send power-on notification
    log_i("Sending power-on notification...");
    String powerOnMsg = "Device powered on";
    
    if (modem.sendSMS(SMS_TARGET, powerOnMsg)) {
        log_i("[OK] Power-on SMS sent successfully");
    } else {
        log_i("[ERROR] Failed to send power-on SMS");
    }
}




void handleSMS()
{
    ReceivedSMS sms;
    if (!reader.readNext(sms)) return;

    bool forwarded;
    if (sms.number == SMS_TARGET) {
        processor.process(sms);
        forwarded = true;
    } else {
        forwarded = forwarder.forward(sms);
    }

    if (forwarded) {
        reader.deleteMessage(sms.index);
    } else {
        log_i("[INFO] SMS at index %d kept as read (forward failed)", sms.index);
    }
}

void loop()
{
    // Check for incoming SMS periodically
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 2000) {
        lastCheck = millis();
        handleSMS();
    }

    // Check battery level every minute
    static unsigned long lastBatteryCheck = 0;
    static bool batteryAlertSent = false;
    static bool usbAlertSent = false;
    if (millis() - lastBatteryCheck > 60000) {
        lastBatteryCheck = millis();
        if (SMSProcessor::isPoweredByBattery()) {
            if (!batteryAlertSent) {
                log_i("Battery power detected, sending SMS...");
                if (sender.send(SMS_TARGET, "Device is now on battery power")) {
                    log_i("[OK] Battery alert SMS sent successfully");
                    batteryAlertSent = true;
                    usbAlertSent = false;
                } else {
                    log_i("[ERROR] Failed to send battery alert SMS");
                }
            }
        } else {
            if (!usbAlertSent && batteryAlertSent) {
                log_i("Back on USB power, sending SMS...");
                if (sender.send(SMS_TARGET, "Device is now on USB power")) {
                    log_i("[OK] USB alert SMS sent successfully");
                    usbAlertSent = true;
                } else {
                    log_i("[ERROR] Failed to send USB alert SMS");
                }
            }
            batteryAlertSent = false;
        }
    }

    if (SerialAT.available()) {
        Serial.write(SerialAT.read());
    }
    if (Serial.available()) {
        SerialAT.write(Serial.read());
    }
    delay(100);
}

#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGO-T-A76XX/tree/main/lib) to the arduino libraries directory , See README"
#endif
