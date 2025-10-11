//
// Created by Nicholas Wiersma on 2025/10/10.
//

#ifndef FIRMWARE_LOGGER_H
#define FIRMWARE_LOGGER_H

#include <Arduino.h>

class logger {
public:
    logger();

    void errorf(const char *format, ...);
    void infof(const char *format, ...);
    void debugf(const char *format, ...);

protected:
    bool restart;
    FsFile msgFile;

    enum LVL {
        UNKNOWN,
        DEBUG,
        INFO,
        ERROR
    };

    void writef(LVL lvl, const char *format, va_list args);
    void write(LVL lvl, const char *buffer, size_t size);

private:
    size_t bufLen;
};

#endif //FIRMWARE_LOGGER_H
