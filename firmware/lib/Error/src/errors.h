#pragma once

#include <cstdarg>
#include <cstdlib>
#include <cstring>

class Error {
public:
    virtual             ~Error() = default;
    virtual const char *what() = 0;
};

class ErrorString : public Error {
public:
    explicit ErrorString(const char *msg)
        : s(msg) {
    }

    const char *what() override {
        return s;
    }

private:
    const char *s;
};

inline Error *makeError(const char *msg) {
    return new ErrorString(msg);
}
