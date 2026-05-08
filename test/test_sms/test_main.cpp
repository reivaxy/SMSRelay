#include <unity.h>

#include "SMSForwarder.h"
#include "SMSProcessor.h"
#include "SMSReader.h"
#include "SMSSender.h"

// NullStream: a Stream that always reports no bytes available
class NullStream : public Stream {};

// CaptureStream records outbound PDU text written by SMSSender UCS2 path.
class CaptureStream : public Stream {
public:
    std::string captured;

    void print(const String &s) override { captured += s.c_str(); }
    void print(const char *s) override { if (s) captured += s; }
    size_t write(uint8_t b) override {
        captured.push_back((char)b);
        return 1;
    }
};

// Shared mock hardware object — reused across all tests
static TinyGsm g_modem;

// Helpers to rebuild mock state before each test
void setUp() {
    g_modem.clearSent();
    g_modem.sendSMSResult = true;
}
void tearDown() {}

// =========================================================================
// SMSSender — static method tests
// =========================================================================

void test_needsUCS2_ascii_returns_false() {
    TEST_ASSERT_FALSE(SMSSender::needsUCS2("Hello World 123"));
}

void test_needsUCS2_nonascii_returns_true() {
    // UTF-8 encoding of "é" (U+00E9): 0xC3 0xA9
    TEST_ASSERT_TRUE(SMSSender::needsUCS2("\xC3\xA9"));
}

void test_needsUCS2_empty_returns_false() {
    TEST_ASSERT_FALSE(SMSSender::needsUCS2(""));
}

void test_utf8ToUCS2Hex_ascii() {
    // "Hi" → U+0048 U+0069 → "00480069"
    String result = SMSSender::utf8ToUCS2Hex("Hi");
    TEST_ASSERT_EQUAL_STRING("00480069", result.c_str());
}

void test_utf8ToUCS2Hex_2byte_utf8() {
    // "é" (U+00E9) — UTF-8: 0xC3 0xA9 → "00E9"
    String result = SMSSender::utf8ToUCS2Hex("\xC3\xA9");
    TEST_ASSERT_EQUAL_STRING("00E9", result.c_str());
}

void test_utf8ToUCS2Hex_2byte_utf8_egrave() {
    // "è" (U+00E8) — UTF-8: 0xC3 0xA8 → "00E8"
    String result = SMSSender::utf8ToUCS2Hex("\xC3\xA8");
    TEST_ASSERT_EQUAL_STRING("00E8", result.c_str());
}

void test_utf8ToUCS2Hex_2byte_utf8_ccedilla() {
    // "ç" (U+00E7) — UTF-8: 0xC3 0xA7 → "00E7"
    String result = SMSSender::utf8ToUCS2Hex("\xC3\xA7");
    TEST_ASSERT_EQUAL_STRING("00E7", result.c_str());
}

void test_utf8ToUCS2Hex_3byte_utf8() {
    // "€" (U+20AC) — UTF-8: 0xE2 0x82 0xAC → "20AC"
    String result = SMSSender::utf8ToUCS2Hex("\xE2\x82\xAC");
    TEST_ASSERT_EQUAL_STRING("20AC", result.c_str());
}

void test_utf8ToUCS2Hex_mixed() {
    // "A€" → U+0041 U+20AC → "004120AC"
    String result = SMSSender::utf8ToUCS2Hex("A\xE2\x82\xAC");
    TEST_ASSERT_EQUAL_STRING("004120AC", result.c_str());
}

// =========================================================================
// SMSReader — static method tests
// =========================================================================

void test_isHexUCS2_valid_uppercase() {
    TEST_ASSERT_TRUE(SMSReader::isHexUCS2("00480069"));
}

void test_isHexUCS2_valid_lowercase() {
    TEST_ASSERT_TRUE(SMSReader::isHexUCS2("00ab00cd"));
}

void test_isHexUCS2_empty_returns_false() {
    TEST_ASSERT_FALSE(SMSReader::isHexUCS2(""));
}

void test_isHexUCS2_odd_length_returns_false() {
    TEST_ASSERT_FALSE(SMSReader::isHexUCS2("004"));
}

void test_isHexUCS2_length_not_multiple_of_4_returns_false() {
    // Length 6 — not a multiple of 4
    TEST_ASSERT_FALSE(SMSReader::isHexUCS2("004800"));
}

void test_isHexUCS2_invalid_char_returns_false() {
    TEST_ASSERT_FALSE(SMSReader::isHexUCS2("004G"));
}

void test_decodeUCS2Hex_ascii_word() {
    // "00480069" → "Hi"
    String result = SMSReader::decodeUCS2Hex("00480069");
    TEST_ASSERT_EQUAL_STRING("Hi", result.c_str());
}

void test_decodeUCS2Hex_latin1_char() {
    // "00E9" → UTF-8 "é" (0xC3 0xA9)
    String result = SMSReader::decodeUCS2Hex("00E9");
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9", result.c_str());
}

void test_decodeUCS2Hex_latin1_egrave_char() {
    // "00E8" → UTF-8 "è" (0xC3 0xA8)
    String result = SMSReader::decodeUCS2Hex("00E8");
    TEST_ASSERT_EQUAL_STRING("\xC3\xA8", result.c_str());
}

void test_decodeUCS2Hex_latin1_ccedilla_char() {
    // "00E7" → UTF-8 "ç" (0xC3 0xA7)
    String result = SMSReader::decodeUCS2Hex("00E7");
    TEST_ASSERT_EQUAL_STRING("\xC3\xA7", result.c_str());
}

void test_decodeUCS2Hex_3byte_utf8_char() {
    // "20AC" → UTF-8 "€" (0xE2 0x82 0xAC)
    String result = SMSReader::decodeUCS2Hex("20AC");
    TEST_ASSERT_EQUAL_STRING("\xE2\x82\xAC", result.c_str());
}

void test_decodeUCS2Hex_invalid_passthrough() {
    // Non-hex input should be returned unchanged
    String result = SMSReader::decodeUCS2Hex("Hello");
    TEST_ASSERT_EQUAL_STRING("Hello", result.c_str());
}

void test_decodeUCS2Hex_roundtrip_with_utf8ToUCS2Hex() {
    const char *original = "Hi\xC3\xA9";  // "Hié"
    String hex    = SMSSender::utf8ToUCS2Hex(original);
    String decoded = SMSReader::decodeUCS2Hex(hex);
    TEST_ASSERT_EQUAL_STRING(original, decoded.c_str());
}

void test_send_normalizes_mojibake_eacute_to_single_uCS2_codepoint() {
    CaptureStream stream;
    SMSSender sender(g_modem, stream);

    const char mojibakeEAcute[] = { (char)0xC3, (char)0x83, (char)0xC2, (char)0xA9, 0 };
    bool sent = sender.send("+1111", String(mojibakeEAcute));

    TEST_ASSERT_TRUE(sent);
    TEST_ASSERT_TRUE(stream.captured.find("00E9") != std::string::npos);
    TEST_ASSERT_TRUE(stream.captured.find("00C3008300C200A9") == std::string::npos);
}

void test_send_normalizes_mojibake_egrave_pair_to_two_uCS2_codepoints() {
    CaptureStream stream;
    SMSSender sender(g_modem, stream);

    const char mojibakeEAcute[] = { (char)0xC3, (char)0x83, (char)0xC2, (char)0xA9, 0 };
    const char mojibakeEGrave[] = { (char)0xC3, (char)0x83, (char)0xC2, (char)0xA8, 0 };
    String text = String(mojibakeEAcute) + String(mojibakeEGrave);
    bool sent = sender.send("+1111", text);

    TEST_ASSERT_TRUE(sent);
    TEST_ASSERT_TRUE(stream.captured.find("00E900E8") != std::string::npos);
}

// =========================================================================
// SMSProcessor — command routing tests
// =========================================================================

void test_processor_status_command() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "STATUS";
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    TEST_ASSERT_TRUE(g_modem.sentMessages.size() >= 1);
    TEST_ASSERT_EQUAL_STRING("+1111", g_modem.sentMessages.back().number.c_str());
    TEST_ASSERT_TRUE(g_modem.sentMessages.back().text.find("Status:") != std::string::npos);
}

void test_processor_status_command_case_insensitive() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "status";  // lowercase
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    TEST_ASSERT_TRUE(g_modem.sentMessages.size() >= 1);
    TEST_ASSERT_TRUE(g_modem.sentMessages.back().text.find("Status:") != std::string::npos);
}

void test_processor_for_command_success() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "FOR:+4242 Hello there";
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    // Two SMS: delivery to +4242, then OK confirmation to +1111
    TEST_ASSERT_EQUAL(2, (int)g_modem.sentMessages.size());
    TEST_ASSERT_EQUAL_STRING("+4242", g_modem.sentMessages[0].number.c_str());
    TEST_ASSERT_EQUAL_STRING("Hello there", g_modem.sentMessages[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("+1111", g_modem.sentMessages[1].number.c_str());
    TEST_ASSERT_TRUE(g_modem.sentMessages[1].text.find("OK:") != std::string::npos);
}

void test_processor_for_command_failure_sends_error() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    g_modem.sendSMSResult = false;  // first send fails

    ReceivedSMS sms;
    sms.text = "FOR:+4242 Hello";
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    // Delivery attempt + ERROR report back to controller
    TEST_ASSERT_EQUAL(2, (int)g_modem.sentMessages.size());
    TEST_ASSERT_TRUE(g_modem.sentMessages[1].text.find("ERROR:") != std::string::npos);
}

void test_processor_for_command_missing_body() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "FOR:+4242";  // no message body after the number
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_TRUE(g_modem.sentMessages[0].text.find("ERROR:") != std::string::npos);
}

void test_processor_read_invalid_index_zero() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "READ 0";
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_TRUE(g_modem.sentMessages[0].text.find("ERROR:") != std::string::npos);
}

void test_processor_delete_invalid_index_zero() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "DELETE 0";
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_TRUE(g_modem.sentMessages[0].text.find("ERROR:") != std::string::npos);
}

void test_processor_list_command_no_stored_messages() {
    // NullStream returns no bytes → SMSReader::readAt always returns false
    // → handleListCommand sends "No messages stored"
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSReader  reader(g_modem, stream);
    SMSProcessor proc(sender, "+1111", reader);

    ReceivedSMS sms;
    sms.text = "LIST";
    sms.number = "+9999";
    sms.index = 1;
    proc.process(sms);

    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_TRUE(g_modem.sentMessages[0].text.find("No messages") != std::string::npos);
}

// =========================================================================
// SMSForwarder — forwarding logic tests
// =========================================================================

void test_forwarder_sends_header_and_body_for_new_sender() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSForwarder forwarder(sender, "+1111");

    ReceivedSMS sms;
    sms.text = "Hello from 9999";
    sms.number = "+9999";
    sms.timestamp = "25/01/01,12:00:00+00";
    sms.index = 1;

    bool ok = forwarder.forward(sms);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(2, (int)g_modem.sentMessages.size());
    // Header contains sender number
    TEST_ASSERT_TRUE(g_modem.sentMessages[0].text.find("+9999") != std::string::npos);
    // Body is the original text
    TEST_ASSERT_EQUAL_STRING("Hello from 9999", g_modem.sentMessages[1].text.c_str());
}

void test_forwarder_suppresses_header_for_same_sender_in_window() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSForwarder forwarder(sender, "+1111");

    ReceivedSMS sms;
    sms.text = "First message";
    sms.number = "+9999";
    sms.timestamp = "25/01/01,12:00:00+00";
    sms.index = 1;
    forwarder.forward(sms);  // header sent here
    g_modem.clearSent();

    // Second message from same number — millis() still returns 0, within window
    sms.text = "Second message";
    sms.index = 2;
    forwarder.forward(sms);

    // Only body, no header
    TEST_ASSERT_EQUAL(1, (int)g_modem.sentMessages.size());
    TEST_ASSERT_EQUAL_STRING("Second message", g_modem.sentMessages[0].text.c_str());
}

void test_forwarder_sends_header_for_different_sender() {
    NullStream stream;
    SMSSender  sender(g_modem, stream);
    SMSForwarder forwarder(sender, "+1111");

    ReceivedSMS sms;
    sms.text = "From A";
    sms.number = "+1111";
    sms.timestamp = "25/01/01,12:00:00+00";
    sms.index = 1;
    forwarder.forward(sms);
    g_modem.clearSent();

    // Different sender number — must get a header
    sms.number = "+2222";
    sms.text = "From B";
    sms.index = 2;
    forwarder.forward(sms);

    TEST_ASSERT_EQUAL(2, (int)g_modem.sentMessages.size());
    TEST_ASSERT_TRUE(g_modem.sentMessages[0].text.find("+2222") != std::string::npos);
}

// =========================================================================
// Entry point
// =========================================================================

int main(int, char **) {
    UNITY_BEGIN();

    // SMSSender::needsUCS2
    RUN_TEST(test_needsUCS2_ascii_returns_false);
    RUN_TEST(test_needsUCS2_nonascii_returns_true);
    RUN_TEST(test_needsUCS2_empty_returns_false);

    // SMSSender::utf8ToUCS2Hex
    RUN_TEST(test_utf8ToUCS2Hex_ascii);
    RUN_TEST(test_utf8ToUCS2Hex_2byte_utf8);
    RUN_TEST(test_utf8ToUCS2Hex_2byte_utf8_egrave);
    RUN_TEST(test_utf8ToUCS2Hex_2byte_utf8_ccedilla);
    RUN_TEST(test_utf8ToUCS2Hex_3byte_utf8);
    RUN_TEST(test_utf8ToUCS2Hex_mixed);

    // SMSReader::isHexUCS2
    RUN_TEST(test_isHexUCS2_valid_uppercase);
    RUN_TEST(test_isHexUCS2_valid_lowercase);
    RUN_TEST(test_isHexUCS2_empty_returns_false);
    RUN_TEST(test_isHexUCS2_odd_length_returns_false);
    RUN_TEST(test_isHexUCS2_length_not_multiple_of_4_returns_false);
    RUN_TEST(test_isHexUCS2_invalid_char_returns_false);

    // SMSReader::decodeUCS2Hex
    RUN_TEST(test_decodeUCS2Hex_ascii_word);
    RUN_TEST(test_decodeUCS2Hex_latin1_char);
    RUN_TEST(test_decodeUCS2Hex_latin1_egrave_char);
    RUN_TEST(test_decodeUCS2Hex_latin1_ccedilla_char);
    RUN_TEST(test_decodeUCS2Hex_3byte_utf8_char);
    RUN_TEST(test_decodeUCS2Hex_invalid_passthrough);
    RUN_TEST(test_decodeUCS2Hex_roundtrip_with_utf8ToUCS2Hex);
    RUN_TEST(test_send_normalizes_mojibake_eacute_to_single_uCS2_codepoint);
    RUN_TEST(test_send_normalizes_mojibake_egrave_pair_to_two_uCS2_codepoints);

    // SMSProcessor
    RUN_TEST(test_processor_status_command);
    RUN_TEST(test_processor_status_command_case_insensitive);
    RUN_TEST(test_processor_for_command_success);
    RUN_TEST(test_processor_for_command_failure_sends_error);
    RUN_TEST(test_processor_for_command_missing_body);
    RUN_TEST(test_processor_read_invalid_index_zero);
    RUN_TEST(test_processor_delete_invalid_index_zero);
    RUN_TEST(test_processor_list_command_no_stored_messages);

    // SMSForwarder
    RUN_TEST(test_forwarder_sends_header_and_body_for_new_sender);
    RUN_TEST(test_forwarder_suppresses_header_for_same_sender_in_window);
    RUN_TEST(test_forwarder_sends_header_for_different_sender);

    return UNITY_END();
}
