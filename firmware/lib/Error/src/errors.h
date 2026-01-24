#pragma once

#include <cstdarg>
#include <cstdlib>
#include <cstring>

class error {
public:
    virtual             ~error() = default;
    virtual const char *Error() = 0;
};

class errorString : public error {
public:
    explicit errorString(const char *msg)
        : s(msg) {
    }

    const char *Error() override {
        return s;
    }

private:
    const char *s;
};

inline error *newError(const char *msg) {
    return new errorString(msg);
}

