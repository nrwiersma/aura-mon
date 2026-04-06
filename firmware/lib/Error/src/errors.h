#pragma once

class error {
public:
    constexpr error() : s(nullptr) {}
    constexpr explicit error(const char *msg) : s(msg) {}

    explicit operator bool() const { return s != nullptr; }

    const char *Error() const { return s; }

private:
    const char *s;
};

inline error newError(const char *msg) {
    return error(msg);
}

