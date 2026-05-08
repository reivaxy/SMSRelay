#pragma once

#include "Arduino.h"
#include <string>
#include <utility>
#include <vector>

// GF("literal") — in progmem-less builds just passes the string through
#define GF(A) A

// -------------------------------------------------------------------------
// TinyGsm mock
// Records sendSMS calls so tests can assert on them.
// All AT command methods are variadic no-ops; waitResponse always returns 1.
// -------------------------------------------------------------------------
class TinyGsm {
public:
    struct SentSMS {
        std::string number;
        std::string text;
    };

    std::vector<SentSMS> sentMessages;
    bool sendSMSResult = true;

    bool sendSMS(const String &number, const String &text) {
        sentMessages.push_back({number.c_str(), text.c_str()});
        return sendSMSResult;
    }

    // Accept any number / types of AT arguments and ignore them
    template <typename... Args>
    void sendAT(Args &&...) {}

    // waitResponse(timeout) or waitResponse(timeout, expected) — always OK
    template <typename... Args>
    int waitResponse(Args &&...) { return 1; }

    void clearSent() { sentMessages.clear(); }
};
