#include <unity.h>

#include "BatteryProcessor.h"
#include "SMSSender.h"

// NullStream: minimal Stream stub for the SMSSender serialAT parameter
class NullStream : public Stream {};

// Shared mock modem
static TinyGsm g_modem;

void setUp() {
    g_modem.clearSent();
    g_modem.sendSMSResult = true;
    g_analogReadValue = 0;
}
void tearDown() {}

// Helper: advance millis() past the 60-second throttle window.
// BatteryProcessor::check() skips if millis() - _lastCheck < 60000.
// millis() increments by 1000 per call, so 62 calls = 62 s.
static void advanceMillis60s() {
    for (int i = 0; i < 62; i++) millis();
}

// Run one unthrottled check on bp.
static void runCheck(BatteryProcessor &bp) {
    advanceMillis60s();
    bp.check();
}

static const char *TARGET = "+1234567890";

// ─────────────────────────────────────────────────────────────────────────────
// USB power — no alert expected
// ─────────────────────────────────────────────────────────────────────────────

void test_usb_power_adc_zero_no_sms() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 0;  // analogRead = 0 → USB branch
    runCheck(bp);
    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_usb_power_adc_above_threshold_no_sms() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2400;  // >= BAT_ADC_THRESHOLD (2300) → USB
    runCheck(bp);
    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_usb_adc_at_exact_threshold_no_sms() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2300;  // == BAT_ADC_THRESHOLD → USB (not < threshold)
    runCheck(bp);
    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Battery power detection
// ─────────────────────────────────────────────────────────────────────────────

void test_battery_power_sends_alert() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;  // 1800 <= 2000 < 2300 → battery range
    runCheck(bp);
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
}

void test_battery_alert_sent_to_target_number() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);
    TEST_ASSERT_EQUAL_STRING(TARGET, g_modem.sentMessages[0].number.c_str());
}

void test_battery_alert_text_contains_battery_power() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("battery power"));
}

void test_battery_alert_text_contains_adc_value() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("2000"));
}

void test_battery_adc_just_below_threshold_sends_alert() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2299;  // one below BAT_ADC_THRESHOLD → battery
    runCheck(bp);
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("battery power"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Battery alert deduplication
// ─────────────────────────────────────────────────────────────────────────────

void test_battery_alert_not_resent_within_throttle_window() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);
    int countAfterFirst = (int)g_modem.sentMessages.size();
    bp.check();  // immediate second call — throttled
    TEST_ASSERT_EQUAL(countAfterFirst, (int)g_modem.sentMessages.size());
}

void test_battery_alert_not_resent_after_60s() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);  // first check — battery alert, _batteryAlertSent = true
    int countAfterFirst = (int)g_modem.sentMessages.size();
    runCheck(bp);  // same ADC, _batteryAlertSent still true → no repeat
    TEST_ASSERT_EQUAL(countAfterFirst, (int)g_modem.sentMessages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Near-empty detection
// ─────────────────────────────────────────────────────────────────────────────

void test_near_empty_sends_alert() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 1500;  // < BAT_ADC_NEAR_EMPTY_THRESHOLD (1800)
    runCheck(bp);
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
}

void test_near_empty_alert_text_contains_near_empty() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 1500;
    runCheck(bp);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("near empty"));
}

void test_near_empty_alert_text_contains_adc_value() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 1500;
    runCheck(bp);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("1500"));
}

// Near-empty fires on first check; on the second check (after 60s), _nearEmptyAlertSent is
// true so the near-empty branch is skipped.  But _batteryAlertSent is still false, so the
// battery-power branch fires.  A third check is fully deduplicated.
void test_near_empty_second_check_sends_battery_power_alert() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 1500;
    runCheck(bp);  // near-empty alert
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    runCheck(bp);  // _nearEmptyAlertSent=true → skip near-empty; !_batteryAlertSent → battery-power
    TEST_ASSERT_EQUAL(2, (int)g_modem.sentMessages.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[1].text.find("battery power"));
    runCheck(bp);  // both flags set → nothing more
    TEST_ASSERT_EQUAL(2, (int)g_modem.sentMessages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// USB restore after battery
// ─────────────────────────────────────────────────────────────────────────────

void test_usb_restore_alert_after_battery() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);  // battery alert → _batteryAlertSent = true
    g_modem.clearSent();
    g_analogReadValue = 2400;
    runCheck(bp);  // USB → sends "USB power"
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("USB power"));
}

void test_usb_restore_not_sent_without_prior_battery_alert() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2400;
    runCheck(bp);  // USB with no prior battery alert → nothing
    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_usb_restore_not_resent_on_second_usb_check() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);  // battery alert
    g_analogReadValue = 2400;
    runCheck(bp);  // USB alert
    int countAfterUsb = (int)g_modem.sentMessages.size();
    runCheck(bp);  // still USB, _batteryAlertSent reset to false → no new alert
    TEST_ASSERT_EQUAL(countAfterUsb, (int)g_modem.sentMessages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// State reset on USB: flags cleared so battery alert fires fresh on next battery read
// ─────────────────────────────────────────────────────────────────────────────

void test_state_reset_on_usb_allows_fresh_battery_alert() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_analogReadValue = 2000;
    runCheck(bp);  // battery alert
    g_analogReadValue = 2400;
    runCheck(bp);  // USB alert + state reset
    g_modem.clearSent();
    g_analogReadValue = 2000;
    runCheck(bp);  // back on battery — fresh alert expected
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages[0].text.find("battery power"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Send failure: flag not set → alert retried on next check
// ─────────────────────────────────────────────────────────────────────────────

// Mock records the call even on failure; the key is that the flag stays false so
// the next check attempts again.
void test_battery_send_failure_flag_not_set_retried() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_modem.sendSMSResult = false;
    g_analogReadValue = 2000;
    runCheck(bp);  // call recorded, returns false → _batteryAlertSent stays false
    int countAfterFail = (int)g_modem.sentMessages.size();
    g_modem.sendSMSResult = true;
    runCheck(bp);  // retries — flag now set
    TEST_ASSERT_EQUAL(countAfterFail + 1, (int)g_modem.sentMessages.size());
    // After the successful retry, no further attempts on subsequent checks
    runCheck(bp);
    TEST_ASSERT_EQUAL(countAfterFail + 1, (int)g_modem.sentMessages.size());
}

void test_near_empty_send_failure_flag_not_set_retried() {
    NullStream atStream;
    SMSSender sender(g_modem, atStream);
    BatteryProcessor bp(sender, TARGET);
    g_modem.sendSMSResult = false;
    g_analogReadValue = 1500;
    runCheck(bp);  // call recorded, returns false → _nearEmptyAlertSent stays false
    int countAfterFail = (int)g_modem.sentMessages.size();
    g_modem.sendSMSResult = true;
    runCheck(bp);  // retries near-empty
    TEST_ASSERT_EQUAL(countAfterFail + 1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
        g_modem.sentMessages.back().text.find("near empty"));
}

// ─────────────────────────────────────────────────────────────────────────────
// readBatADC static method
// ─────────────────────────────────────────────────────────────────────────────

void test_read_bat_adc_averages_analogread() {
    g_analogReadValue = 1800;
    TEST_ASSERT_EQUAL(1800, BatteryProcessor::readBatADC());
}

void test_read_bat_adc_returns_zero_when_analogread_zero() {
    g_analogReadValue = 0;
    TEST_ASSERT_EQUAL(0, BatteryProcessor::readBatADC());
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // USB — no alerts
    RUN_TEST(test_usb_power_adc_zero_no_sms);
    RUN_TEST(test_usb_power_adc_above_threshold_no_sms);
    RUN_TEST(test_usb_adc_at_exact_threshold_no_sms);

    // Battery power detection
    RUN_TEST(test_battery_power_sends_alert);
    RUN_TEST(test_battery_alert_sent_to_target_number);
    RUN_TEST(test_battery_alert_text_contains_battery_power);
    RUN_TEST(test_battery_alert_text_contains_adc_value);
    RUN_TEST(test_battery_adc_just_below_threshold_sends_alert);

    // Deduplication
    RUN_TEST(test_battery_alert_not_resent_within_throttle_window);
    RUN_TEST(test_battery_alert_not_resent_after_60s);

    // Near-empty
    RUN_TEST(test_near_empty_sends_alert);
    RUN_TEST(test_near_empty_alert_text_contains_near_empty);
    RUN_TEST(test_near_empty_alert_text_contains_adc_value);
    RUN_TEST(test_near_empty_second_check_sends_battery_power_alert);

    // USB restore
    RUN_TEST(test_usb_restore_alert_after_battery);
    RUN_TEST(test_usb_restore_not_sent_without_prior_battery_alert);
    RUN_TEST(test_usb_restore_not_resent_on_second_usb_check);
    RUN_TEST(test_state_reset_on_usb_allows_fresh_battery_alert);

    // Send failure / retry
    RUN_TEST(test_battery_send_failure_flag_not_set_retried);
    RUN_TEST(test_near_empty_send_failure_flag_not_set_retried);

    // readBatADC
    RUN_TEST(test_read_bat_adc_averages_analogread);
    RUN_TEST(test_read_bat_adc_returns_zero_when_analogread_zero);

    return UNITY_END();
}
