#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>

using std::max;
using std::min;

// Bring stdint types into global scope
using std::int16_t;
using std::int32_t;
using std::int8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

// Arduino constants
#define HIGH 1
#define LOW 0

// Logging macros — no-ops for host tests
#define log_i(fmt, ...) ((void)0)
#define log_e(fmt, ...) ((void)0)
#define log_w(fmt, ...) ((void)0)
#define log_d(fmt, ...) ((void)0)

// -------------------------------------------------------------------------
// String class — thin wrapper around std::string
// -------------------------------------------------------------------------
class String {
public:
    String() : str_() {}
    String(const char *s) : str_(s ? s : "") {}
    String(const std::string &s) : str_(s) {}
    String(std::string &&s) : str_(std::move(s)) {}
    explicit String(int n) : str_(std::to_string(n)) {}
    explicit String(unsigned int n) : str_(std::to_string(n)) {}
    explicit String(long n) : str_(std::to_string(n)) {}
    explicit String(unsigned long n) : str_(std::to_string(n)) {}
    explicit String(char c) : str_(1, c) {}

    // Accessors
    unsigned int length() const { return (unsigned int)str_.length(); }
    bool isEmpty() const { return str_.empty(); }
    const char *c_str() const { return str_.c_str(); }
    char operator[](int i) const { return str_[i]; }
    char operator[](unsigned int i) const { return str_[i]; }

    // Assignment / concatenation
    String &operator+=(const String &rhs) {
        str_ += rhs.str_;
        return *this;
    }
    String &operator+=(const char *rhs) {
        if (rhs) str_ += rhs;
        return *this;
    }
    String &operator+=(char c) {
        str_ += c;
        return *this;
    }
    String operator+(const String &rhs) const { return String(str_ + rhs.str_); }
    String operator+(const char *rhs) const { return String(str_ + (rhs ? rhs : "")); }
    String operator+(char c) const { return String(str_ + c); }
    friend String operator+(const char *lhs, const String &rhs) {
        return String(std::string(lhs ? lhs : "") + rhs.str_);
    }
    friend String operator+(char lhs, const String &rhs) {
        return String(std::string(1, lhs) + rhs.str_);
    }

    // Comparison
    bool operator==(const String &rhs) const { return str_ == rhs.str_; }
    bool operator==(const char *rhs) const { return str_ == (rhs ? rhs : ""); }
    bool operator!=(const String &rhs) const { return str_ != rhs.str_; }
    bool operator!=(const char *rhs) const { return str_ != (rhs ? rhs : ""); }

    // Predicates
    bool startsWith(const char *prefix) const {
        if (!prefix) return false;
        return str_.find(prefix) == 0;
    }
    bool startsWith(const String &prefix) const { return str_.find(prefix.str_) == 0; }

    bool equalsIgnoreCase(const char *s) const {
        if (!s) return false;
        if (str_.length() != strlen(s)) return false;
        for (size_t i = 0; i < str_.length(); i++) {
            if (tolower((unsigned char)str_[i]) != tolower((unsigned char)s[i])) return false;
        }
        return true;
    }
    bool equalsIgnoreCase(const String &s) const { return equalsIgnoreCase(s.str_.c_str()); }

    // Substring
    String substring(int from) const {
        if (from >= (int)str_.length()) return String("");
        return String(str_.substr(from));
    }
    String substring(int from, int to) const {
        if (from >= (int)str_.length()) return String("");
        int len = to - from;
        if (len <= 0) return String("");
        return String(str_.substr(from, len));
    }

    // Mutators
    void toUpperCase() {
        for (auto &c : str_) c = (char)toupper((unsigned char)c);
    }
    void trim() {
        auto notSpace = [](char c) { return !isspace((unsigned char)c); };
        str_.erase(str_.begin(), std::find_if(str_.begin(), str_.end(), notSpace));
        str_.erase(std::find_if(str_.rbegin(), str_.rend(), notSpace).base(), str_.end());
    }
    void replace(const char *from, const char *to) {
        if (!from || !to) return;
        std::string f(from), t(to);
        size_t pos = 0;
        while ((pos = str_.find(f, pos)) != std::string::npos) {
            str_.replace(pos, f.length(), t);
            pos += t.length();
        }
    }
    void replace(const String &from, const String &to) { replace(from.str_.c_str(), to.str_.c_str()); }

    // Search
    int indexOf(const char *needle, int from = 0) const {
        if (!needle) return -1;
        auto pos = str_.find(needle, (size_t)from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(const String &needle, int from = 0) const { return indexOf(needle.str_.c_str(), from); }
    int indexOf(char c, int from = 0) const {
        auto pos = str_.find(c, (size_t)from);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    // Conversion
    int toInt() const {
        try {
            return std::stoi(str_);
        } catch (...) {
            return 0;
        }
    }

    // Allow comparison to std::string in tests
    std::string stdStr() const { return str_; }

private:
    std::string str_;
};

// -------------------------------------------------------------------------
// Hardware stubs
// -------------------------------------------------------------------------
inline unsigned long millis() {
    static unsigned long ms = 0;
    return (ms += 1000); // advance 1s per call so hardware timeout loops always exit
}
inline void delay(int) {}
inline int analogRead(int) { return 0; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}

// -------------------------------------------------------------------------
// Stream / Serial stubs
// -------------------------------------------------------------------------
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual void print(const String &) {}
    virtual void print(const char *) {}
    virtual void print(int) {}
    virtual size_t write(uint8_t) { return 0; }
    size_t write(char c) { return write((uint8_t)c); }
    virtual void flush() {}
};

class SerialClass : public Stream {
    std::deque<char> inputQueue_;
public:
    // Test helpers
    void feedInput(const char *s) { while (*s) inputQueue_.push_back(*s++); }
    void clearInput() { inputQueue_.clear(); }

    int available() override { return inputQueue_.empty() ? 0 : 1; }
    int read() override {
        if (inputQueue_.empty()) return -1;
        char c = inputQueue_.front(); inputQueue_.pop_front();
        return (unsigned char)c;
    }
    void print(const String &) override {}
    void print(const char *) override {}
    void print(int) override {}
    size_t write(uint8_t) override { return 0; }
    void flush() override {}
    void begin(int) {}
    void println(const char *) {}
    void println(const String &) {}
    void println(int) {}
    void println() {}
    template <typename... Args>
    void printf(const char *, Args &&...) {}
};

inline SerialClass Serial;
