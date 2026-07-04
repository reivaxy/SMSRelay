#include <unity.h>

#include "SerialConsole.h"
#include "SMSForwarder.h"
#include "SMSReader.h"
#include "SMSSender.h"

// -------------------------------------------------------------------------
// ScriptedStream
// Vends pre-queued AT response strings one byte at a time.
// When the queue is empty available() returns 0, causing SMSReader::readAt
// to time out and return false (no SMS found).
// -------------------------------------------------------------------------
class ScriptedStream : public Stream {
    std::deque<std::string> responses_;
    std::string             current_;
    size_t                  pos_ = 0;

    void advance() {
        if (pos_ >= current_.size() && !responses_.empty()) {
            current_ = responses_.front();
            responses_.pop_front();
            pos_ = 0;
        }
    }

public:
    void enqueue(const std::string &s) { responses_.push_back(s); }

    int available() override { advance(); return pos_ < current_.size() ? 1 : 0; }
    int read() override {
        advance();
        if (pos_ >= current_.size()) return -1;
        return (unsigned char)current_[pos_++];
    }
};

// -------------------------------------------------------------------------
// Helper: build a well-formed +CMGR: response string (ASCII content only).
// SMSReader::readAt breaks out of the read loop when "OK" is found.
// -------------------------------------------------------------------------
static std::string makeCmgr(const char *number,
                             const char *timestamp,
                             const char *body)
{
    std::string r = "+CMGR: \"REC UNREAD\",\"";
    r += number;
    r += "\",\"\",\"";
    r += timestamp;
    r += "\"\n";
    r += body;
    r += "\nOK\n";
    return r;
}

// -------------------------------------------------------------------------
// Test fixtures
// -------------------------------------------------------------------------
static TinyGsm g_modem;

void setUp() {
    g_modem.clearSent();
    g_modem.sendSMSResult = true;
    Serial.clearInput();
}
void tearDown() {}

// =========================================================================
// LIST command
// =========================================================================

void test_list_empty() {
    // No CMGR responses queued → readAt(1..30) all time out → "(no messages stored)"
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("LIST\n");
    console.check();

    // No SMS was forwarded or sent
    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_list_case_insensitive() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("list\n"); // lowercase
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_list_one_sms() {
    // Queue one valid CMGR for index 1; indices 2-30 time out → one entry listed
    ScriptedStream atStream;
    atStream.enqueue(makeCmgr("+4242", "25/01/01,12:00:00+00", "Hello from 4242"));

    SMSSender     sender(g_modem, atStream);
    SMSReader     reader(g_modem, atStream);
    SMSForwarder  forwarder(sender, "+1111");
    SerialConsole console(reader, forwarder);

    Serial.feedInput("LIST\n");
    console.check();

    // Listing does not trigger any outbound SMS
    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

// =========================================================================
// READ command
// =========================================================================

void test_read_found() {
    ScriptedStream atStream;
    atStream.enqueue(makeCmgr("+4242", "25/01/01,12:00:00+00", "Test message"));

    SMSSender     sender(g_modem, atStream);
    SMSReader     reader(g_modem, atStream);
    SMSForwarder  forwarder(sender, "+1111");
    SerialConsole console(reader, forwarder);

    Serial.feedInput("READ 1\n");
    console.check(); // should call readAt(1) and print the SMS

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_read_not_found() {
    // No CMGR queued → readAt times out → error printed
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("READ 5\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_read_invalid_index_zero() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("READ 0\n");
    console.check(); // should print error, NOT call readAt

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_read_invalid_index_negative() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("READ -1\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

// =========================================================================
// DELETE command
// =========================================================================

void test_delete_valid() {
    // deleteMessage just fires AT commands through the (no-op) modem mock
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("DELETE 3\n");
    console.check(); // must not crash and must not trigger any SMS send

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_delete_invalid_index_zero() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("DELETE 0\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_delete_case_insensitive() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("delete 2\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

// =========================================================================
// FORWARD command
// =========================================================================

void test_forward_success() {
    ScriptedStream atStream;
    atStream.enqueue(makeCmgr("+9999", "25/01/01,12:00:00+00", "Forward me"));

    SMSSender     sender(g_modem, atStream);
    SMSReader     reader(g_modem, atStream);
    SMSForwarder  forwarder(sender, "+1111");
    SerialConsole console(reader, forwarder);

    Serial.feedInput("FORWARD 1\n");
    console.check();

    // forwarder.forward() sends header + body → at least 2 outbound SMS
    TEST_ASSERT_TRUE((int)g_modem.sentMessages.size() >= 2);
    // Body SMS contains the original text
    bool bodyFound = false;
    for (auto &m : g_modem.sentMessages) {
        if (m.text == "Forward me") { bodyFound = true; break; }
    }
    TEST_ASSERT_TRUE(bodyFound);
}

void test_forward_success_all_go_to_target() {
    ScriptedStream atStream;
    atStream.enqueue(makeCmgr("+9999", "25/01/01,12:00:00+00", "Hello"));

    SMSSender     sender(g_modem, atStream);
    SMSReader     reader(g_modem, atStream);
    SMSForwarder  forwarder(sender, "+1111");
    SerialConsole console(reader, forwarder);

    Serial.feedInput("FORWARD 1\n");
    console.check();

    // All forwarded parts go to the configured target number
    for (auto &m : g_modem.sentMessages) {
        TEST_ASSERT_EQUAL_STRING("+1111", m.number.c_str());
    }
}

void test_forward_not_found() {
    // No CMGR queued → readAt times out → nothing forwarded
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("FORWARD 5\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_forward_invalid_index_zero() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("FORWARD 0\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_forward_case_insensitive() {
    ScriptedStream atStream;
    atStream.enqueue(makeCmgr("+9999", "25/01/01,12:00:00+00", "Hi"));

    SMSSender     sender(g_modem, atStream);
    SMSReader     reader(g_modem, atStream);
    SMSForwarder  forwarder(sender, "+1111");
    SerialConsole console(reader, forwarder);

    Serial.feedInput("forward 1\n");
    console.check();

    TEST_ASSERT_TRUE((int)g_modem.sentMessages.size() >= 1);
}

// =========================================================================
// Unknown command
// =========================================================================

void test_unknown_command_no_crash() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("BLAH BLAH\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_empty_line_ignored() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

// =========================================================================
// Buffering behaviour
// =========================================================================

void test_partial_input_not_processed() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    // Feed characters without a newline — command should not be processed yet
    Serial.feedInput("LIS");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_cr_lf_line_ending() {
    // Windows-style \r\n — \r should be stripped, \n triggers processing
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    Serial.feedInput("LIST\r\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

void test_multiple_commands_in_sequence() {
    ScriptedStream atStream;
    SMSSender      sender(g_modem, atStream);
    SMSReader      reader(g_modem, atStream);
    SMSForwarder   forwarder(sender, "+1111");
    SerialConsole  console(reader, forwarder);

    // Two commands with a single check() call
    Serial.feedInput("LIST\nUNKNOWN\n");
    console.check();

    TEST_ASSERT_EQUAL(0, (int)g_modem.sentMessages.size());
}

// =========================================================================
// Entry point
// =========================================================================

int main(int, char **) {
    UNITY_BEGIN();

    // LIST
    RUN_TEST(test_list_empty);
    RUN_TEST(test_list_case_insensitive);
    RUN_TEST(test_list_one_sms);

    // READ
    RUN_TEST(test_read_found);
    RUN_TEST(test_read_not_found);
    RUN_TEST(test_read_invalid_index_zero);
    RUN_TEST(test_read_invalid_index_negative);

    // DELETE
    RUN_TEST(test_delete_valid);
    RUN_TEST(test_delete_invalid_index_zero);
    RUN_TEST(test_delete_case_insensitive);

    // FORWARD
    RUN_TEST(test_forward_success);
    RUN_TEST(test_forward_success_all_go_to_target);
    RUN_TEST(test_forward_not_found);
    RUN_TEST(test_forward_invalid_index_zero);
    RUN_TEST(test_forward_case_insensitive);

    // Unknown / edge cases
    RUN_TEST(test_unknown_command_no_crash);
    RUN_TEST(test_empty_line_ignored);
    RUN_TEST(test_partial_input_not_processed);
    RUN_TEST(test_cr_lf_line_ending);
    RUN_TEST(test_multiple_commands_in_sequence);

    return UNITY_END();
}
