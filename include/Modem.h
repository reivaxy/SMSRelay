#pragma once
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>

class Modem {
public:
    Modem();

    /**
     * Initialize modem hardware and software.
     * Sets up pins, serial communication, network registration, and SMS configuration.
     * Returns true on success, false on failure.
     */
    bool init();

    /**
     * Get reference to the TinyGsm modem object.
     * Used by other classes that need direct modem access.
     */
    TinyGsm &getModem();

    /**
     * Get reference to the modem serial stream.
     */
    Stream &getSerialStream();

private:
    /**
     * Initialize modem hardware pins (reset, power, DTR, ring, etc.)
     */
    void initPins();

    /**
     * Initialize modem serial communication.
     */
    void initSerial();

    /**
     * Wait for and test modem availability.
     */
    bool testModem();

    /**
     * Wait for SMS registration to complete.
     */
    bool waitSMSReady();

    /**
     * Configure network settings (APN, registration, signal).
     */
    bool configureNetwork();

    /**
     * Configure SMS settings (text mode, indication, charset).
     */
    void configureSMS();

    TinyGsm _modem;
    bool _initialized = false;
};
