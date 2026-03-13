//
// Platform compatibility layer for native testing
//

#pragma once

#ifdef UNIT_TEST

#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>
#include <cstdarg>

// ---------------------------------------------------------------------------
// PROGMEM / flash-string helpers (no-ops on native)
// ---------------------------------------------------------------------------
#define PROGMEM
#define PSTR(s)       (s)
#define memcpy_P      memcpy
#define strncpy_P     strncpy
#define strlen_P      strlen

// ---------------------------------------------------------------------------
// Arduino.h stand-in
// ---------------------------------------------------------------------------
// (Serial.begin / Serial.write captured by MockSerial below)

// ---------------------------------------------------------------------------
// Controllable time stub
// mockNow is set by tests; logger.cpp calls time() which is redirected via
// TestLogger.h to mockTime() so the system time() is not called.
// ---------------------------------------------------------------------------
inline time_t mockNow = 0;

// ---------------------------------------------------------------------------
// Controllable millis() stub
// Set mockMillisManual = true and adjust mockMillisValue to control time
// in tests that care about precise elapsed values.
// ---------------------------------------------------------------------------
inline unsigned long mockMillisValue = 0;
inline bool         mockMillisManual = false;

// ---------------------------------------------------------------------------
// Serial stub – captures everything written to it
// ---------------------------------------------------------------------------
struct MockSerial {
    std::string output;

    void begin(unsigned long /*baud*/) {}

    size_t write(const char *buf, size_t len) {
        output.append(buf, len);
        return len;
    }

    size_t write(uint8_t c) {
        output.push_back(static_cast<char>(c));
        return 1;
    }

    void reset() { output.clear(); }
};

inline MockSerial Serial;

// ---------------------------------------------------------------------------
// SD file-open mode used by logger
// ---------------------------------------------------------------------------
#define FILE_WRITE (O_RDWR | O_CREAT)

// Mock Arduino types and functions
inline unsigned long millis() {
    if (mockMillisManual) {
        return mockMillisValue;
    }
    static unsigned long time = 0;
    time += 10; // Increment by 10ms each call
    return time;
}

// Arduino String backed by std::string – exposes only the methods used by
// production code (isEmpty, indexOf, remove, c_str).
class String {
public:
    std::string s;

    String() = default;
    String(const char *str) : s(str ? str : "") {}
    String(const String &) = default;
    String &operator=(const char *str) { s = str ? str : ""; return *this; }
    String &operator=(const String &) = default;

    bool        isEmpty()            const { return s.empty(); }
    const char *c_str()              const { return s.c_str(); }

    int indexOf(char c, int from = 0) const {
        auto pos = s.find(c, static_cast<size_t>(from));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    void remove(size_t index) { s.erase(index); }
};

struct MockRP2040 {
    bool rebootCalled;

    MockRP2040() : rebootCalled(false) {
    }

    void reset() {
        rebootCalled = false;
        mockMillisManual = false;
        mockMillisValue = 0;
    }

    void reboot() { rebootCalled = true; }
    void wdt_reset() {}
    void memcpyDMA(void *dst, const void *src, size_t sz) { std::memcpy(dst, src, sz); }
};

inline MockRP2040 rp2040;

// Mock mutex operations
typedef struct {} mutex_t;

inline void mutex_init(mutex_t *mtx) { (void) mtx; }
inline void mutex_enter_blocking(mutex_t *mtx) { (void) mtx; }
inline void mutex_exit(mutex_t *mtx) { (void) mtx; }
inline bool mutex_enter_timeout_ms(mutex_t *mtx, uint32_t timeout) {
    (void) mtx;
    (void) timeout;
    return true;
}

// Mock file operations flags
#define O_RDONLY 0x01
#define O_RDWR 0x02
#define O_CREAT 0x0100
#define O_TRUNC 0x0200

// Utility functions
template<typename T>
inline T max(T a, T b) { return (a > b) ? a : b; }

template<typename T>
inline T min(T a, T b) { return (a < b) ? a : b; }

#endif // UNIT_TEST
