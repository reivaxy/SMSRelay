#include "utilities.h"
#include "secret.h"
#include <TinyGsmClient.h>
#include "Modem.h"

// Define the serial console for debug prints, if needed
#define TINY_GSM_DEBUG SerialMon

// Set serial for debug console (to the Serial Monitor, default speed 115200)
#define SerialMon Serial

// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS

#ifdef DUMP_AT_COMMANDS  // if enabled it requires the streamDebugger lib
#include <StreamDebugger.h>
static StreamDebugger* debugger = nullptr;
#endif

Modem::Modem() 
    : _modem(SerialAT), _initialized(false)
{
}

bool Modem::init()
{
    if (_initialized) {
        return true;
    }

    initPins();
    initSerial();

    if (!testModem()) {
        log_e("Failed to test modem");
        return false;
    }

    if (!waitSMSReady()) {
        log_e("Failed to wait for SMS ready");
        return false;
    }

    if (!configureNetwork()) {
        log_e("Failed to configure network");
        return false;
    }

    configureSMS();

    log_i("Init success, ready to receive and forward SMS");
    _initialized = true;
    return true;
}

TinyGsm &Modem::getModem()
{
    return _modem;
}

Stream &Modem::getSerialStream()
{
    return SerialAT;
}

void Modem::initPins()
{
#ifdef BOARD_POWERON_PIN
    /* Set Power control pin output
     * @note      Known issues, ESP32 (V1.2) version of T-A7670, T-A7608,
     *            when using battery power supply mode, BOARD_POWERON_PIN (IO12) must be set to high level after esp32 starts, otherwise a reset will occur.
     */
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);
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

    // Set modem reset pin, reset modem
#ifdef MODEM_RESET_PIN
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
    delay(1000);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

}

void Modem::initSerial()
{
    // Set modem baud
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    log_i("Start modem...");
    
    // Flush any garbage data from the serial buffer
    delay(500);
    flushSerialBuffers();
    
    // Give modem extra time to stabilize after power-on
    delay(MODEM_STABILIZATION_DELAY_MS);
}

void Modem::flushSerialBuffers()
{
    // Clear any pending data in receive buffer
    while (SerialAT.available()) {
        SerialAT.read();
    }
    SerialAT.flush();
}

void Modem::powerDownModem()
{
    log_i("Powering down modem...");
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(MODEM_POWERDOWN_TIME_MS);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(1000);  // Wait a bit after power-down toggle before next operation
}

bool Modem::testModem()
{
    log_i("Testing modem connectivity...");
    
    // Flush any stale data before testing
    flushSerialBuffers();
    
    const int MAX_ATTEMPTS = 15;
    int attempts = 0;
    
    while (!_modem.testAT(TEST_AT_TIMEOUT) && attempts < MAX_ATTEMPTS) {
        delay(300);  // Increased delay between attempts
        attempts++;
    }
    
    if (attempts >= MAX_ATTEMPTS) {
        log_e("Modem test failed after %d attempts", MAX_ATTEMPTS);
        return false;
    }
    
    log_i("Modem test successful after %d attempts", attempts);
    return true;
}

bool Modem::waitSMSReady()
{
    // Wait PB DONE
    log_i("Wait 'SMS Done'.");
    if (!_modem.waitResponse(100000UL, "SMS DONE")) {
        log_i("Can't wait from sms register ....");
        return false;
    }
    return true;
}

bool Modem::configureNetwork()
{
#ifdef NETWORK_APN
    log_i("Set network apn : %s", NETWORK_APN);
    if (!_modem.setNetworkAPN(NETWORK_APN)) {
        log_i("Set network apn error !");
    }
#endif

    // Check network registration status and network signal status
    int16_t sq;
    log_i("Wait for the modem to register with the network.");
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
        status = _modem.getRegistrationStatus();
        switch (status) {
        case REG_UNREGISTERED:
        case REG_SEARCHING:
            sq = _modem.getSignalQuality();
            log_i("[%lu] Signal Quality:%d", millis() / 1000, sq);
            delay(1000);
            break;
        case REG_DENIED:
            log_i("Network registration was rejected, please check if the APN is correct");
            return false;
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

    return true;
}

void Modem::configureSMS()
{
    // Enable SMS text mode
    _modem.sendAT("+CMGF=1");
    _modem.waitResponse();

    // Set up to show SMS indication
    _modem.sendAT("+CNMI=2,1,0,1");
    _modem.waitResponse();

    // Explicitly set UCS2 during init to enforce consistent storage encoding across all modems
    // Some modem variants have different default SMS storage encodings
    _modem.sendAT("+CSCS=\"UCS2\"");
    _modem.waitResponse();
    delay(100);  // Allow modem time to process charset change

    // Verify charset is actually set before switching back to default
    _modem.sendAT("+CSCS=\"IRA\"");
    _modem.waitResponse();
    delay(100);  // Allow modem time to process charset change
    log_i("SMS configuration complete: UCS2 and IRA charsets configured");
}

bool Modem::isConnected()
{
    // Flush any stale data before checking connection
    flushSerialBuffers();
    
    // Test if modem responds to AT commands
    if (!_modem.testAT(TEST_AT_TIMEOUT)) {
        log_w("Modem not responding to AT commands");
        return false;
    }

    // Check if registered with network
    RegStatus status = _modem.getRegistrationStatus();
    bool registered = (status == REG_OK_HOME || status == REG_OK_ROAMING);
    
    if (!registered) {
        log_w("Modem not registered with network. Status: %d", status);
        return false;
    }

    return true;
}

bool Modem::reconnect()
{
    log_i("Attempting to reconnect modem...");
    _initialized = false;
    
    // Perform a proper power cycle to clear any stale modem state
    log_i("Performing modem power cycle...");
    powerDownModem();
    
    // Reset the modem hardware by toggling reset pin
#ifdef MODEM_RESET_PIN
    log_i("Toggling modem reset pin...");
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
    delay(1000);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif
    
    delay(2000);  // Give modem time after power cycle
    
    if (init()) {
        log_i("Modem re-initialized successfully after power cycle");
        return true;
    }

    log_e("Modem reconnection failed");
    return false;
}

void Modem::checkConnection()
{
    // Check connection periodically to avoid excessive checks
    if (millis() - _lastConnectionCheck < CONNECTION_CHECK_INTERVAL) {
        return;
    }
    log_i("Modem connection check");
    
    _lastConnectionCheck = millis();

    if (!isConnected()) {
        log_w("Modem connection lost at %lu ms, attempting reconnection...", millis());
        if (!reconnect()) {
            log_e("Failed to reconnect modem - will retry at next interval");
        } else {
            log_i("Modem successfully reconnected");
        }
    } else {
        log_i("Modem connection check: OK");
    }
}
