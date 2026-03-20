//
// Created by Nicholas Wiersma on 2025/10/10.
//

#pragma once

#include <Arduino.h>

class Logger {
public:
    Logger();

    void errorf(const char *format, ...);
    void infof(const char *format, ...);
    void debugf(const char *format, ...);

protected:
    bool       _restart;
    SafeSdFile _msgFile;

    enum LVL {
        UNKNOWN,
        DEBUG,
        INFO,
        ERROR
    };

    void write(LVL lvl, const char *buffer, size_t size);
};
