#pragma once
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>

#define TEST_AT_TIMEOUT 200  // Timeout for AT command testing in milliseconds
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

    /**
     * Check if modem is still connected and responsive.
     * Returns true if modem is connected, false otherwise.
     */
    bool isConnected();

    /**
     * Attempt to reconnect the modem.
     * Returns true if reconnection successful, false otherwise.
     */
    bool reconnect();

    /**
     * Perform periodic connection check with automatic reconnection.
     * Call this regularly from the main loop to ensure modem stays connected.
     */
    void checkConnection();

    /**
     * Shutdown the modem by powering it down.
     * Used when device needs to be restarted cleanly.
     */
    void shutdown();

    /**
     * Restart the device after cleanly shutting down the modem.
     * Ensures proper modem power-down before ESP restart.
     */
    void restartDevice();

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
     * Flush all pending data from serial buffers to prevent interference with AT commands.
     */
    void flushSerialBuffers();

    /**
     * Power down the modem by toggling PWRKEY pin.
     */
    void powerDownModem();

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
    unsigned long _lastConnectionCheck = 0;
    static const unsigned long CONNECTION_CHECK_INTERVAL = 10000;  // Check every 10 seconds
    static const unsigned long MODEM_POWERDOWN_TIME_MS = 1500;      // Time to hold PWRKEY for power-down
    static const unsigned long MODEM_STABILIZATION_DELAY_MS = 4000; // Extra delay after power-on for stability
};
