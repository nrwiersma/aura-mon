//
// Created by Nicholas Wiersma on 2025/10/10.
//

#pragma once

#include <Arduino.h>

class logger {
public:
    logger();

    void errorf(const char *format, ...);
    void infof(const char *format, ...);
    void debugf(const char *format, ...);

protected:
    bool   _restart;
    FsFile _msgFile;

    enum LVL {
        UNKNOWN,
        DEBUG,
        INFO,
        ERROR
    };

    void write(LVL lvl, const char *buffer, size_t size);
};
