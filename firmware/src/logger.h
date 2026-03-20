//
// Created by Nicholas Wiersma on 2025/10/10.
//

#ifndef FIRMWARE_LOGGER_H
#define FIRMWARE_LOGGER_H

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

#endif //FIRMWARE_LOGGER_H
