//
// Platform compatibility layer for native testing
//

#pragma once

#ifdef UNIT_TEST

#include <stdint.h>
#include <cstring>

// Mock Arduino types and functions
inline unsigned long millis() {
    static unsigned long time = 0;
    time += 10; // Increment by 10ms each call
    return time;
}

class String {
public:
    char * buffer;
    size_t len;

    String() : buffer(nullptr), len(0) {
    }

    String(const char *str) {
        len = strlen(str);
        buffer = new char[len + 1];
        strcpy(buffer, str);
    }

    ~String() { if (buffer) delete[] buffer; }

    void remove(size_t index) {
        if (index < len) {
            buffer[index] = '\0';
            len = index;
        }
    }

    int indexOf(char c, int from = 0) {
        for (size_t i = from; i < len; i++) {
            if (buffer[i] == c) return i;
        }
        return -1;
    }

    const char *c_str() const { return buffer ? buffer : ""; }

    String &operator=(const char *str) {
        if (buffer) delete[] buffer;
        len = strlen(str);
        buffer = new char[len + 1];
        strcpy(buffer, str);
        return *this;
    }
};

struct MockRP2040 {
    bool rebootCalled;

    MockRP2040() : rebootCalled(false) {
    }

    void reset() { rebootCalled = false; }

    void reboot() { rebootCalled = true; }
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
