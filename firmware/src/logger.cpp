//
// Created by Nicholas Wiersma on 2025/10/10.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestLogger.h"
#include "logger.h"
#endif

const char *lvls[] PROGMEM = {"unkn", "dbug", "info", "eror"};

Logger::Logger() : _restart(true) {
    Serial.begin(115200);
}

void Logger::errorf(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    char temp[64];
    char* buffer = temp;
    int n = vsnprintf(temp, sizeof(temp), format, arg);
    va_end(arg);
    if (n < 0) return;
    size_t len = static_cast<size_t>(n);
    if (len > sizeof(temp) - 1) {
        buffer = new char[len + 1];
        va_start(arg, format);
        vsnprintf(buffer, len + 1, format, arg);
        va_end(arg);
    }
    write(ERROR, buffer, len);
    if (buffer != temp) {
        delete[] buffer;
    }
}

void Logger::infof(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    char temp[64];
    char* buffer = temp;
    int n = vsnprintf(temp, sizeof(temp), format, arg);
    va_end(arg);
    if (n < 0) return;
    size_t len = static_cast<size_t>(n);
    if (len > sizeof(temp) - 1) {
        buffer = new char[len + 1];
        if (!buffer) {
            return;
        }
        va_start(arg, format);
        vsnprintf(buffer, len + 1, format, arg);
        va_end(arg);
    }
    write(INFO, buffer, len);
    if (buffer != temp) {
        delete[] buffer;
    }
}

void Logger::debugf(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    char temp[64];
    char* buffer = temp;
    int n = vsnprintf(temp, sizeof(temp), format, arg);
    va_end(arg);
    if (n < 0) return;
    size_t len = static_cast<size_t>(n);
    if (len > sizeof(temp) - 1) {
        buffer = new char[len + 1];
        if (!buffer) {
            return;
        }
        va_start(arg, format);
        vsnprintf(buffer, len + 1, format, arg);
        va_end(arg);
    }
    write(DEBUG, buffer, len);
    if (buffer != temp) {
        delete[] buffer;
    }
}

void Logger::write(const LVL lvl, const char *buffer, size_t size) {
    const size_t lvlLen = strlen(lvls[lvl]);
    // timestamp (20) + space (1) + level + space (1) + message + CRLF (2) + NUL (1)
    size_t bufSize = 20 + 1 + lvlLen + 1 + size + 2 + 1;
    auto * buf = new char[bufSize];
    size_t bufPos = 0;

    time_t now;
    time(&now);
    if (now > 10000000) {
        // We have a system time, use it in the log.
        size_t len = strftime(buf, bufSize, PSTR("%Y-%m-%dT%H:%M:%SZ"), gmtime(&now));
        if (len == 0 || len + 1 >= bufSize) {
            // strftime failed or would overflow; skip the timestamp.
            bufPos = 0;
        } else {
            buf[len] = ' ';
            bufPos = len + 1;
        }
    }

    // Write level.
    memcpy_P(buf + bufPos, lvls[lvl], lvlLen);
    bufPos += lvlLen;
    buf[bufPos++] = ' ';

    // Write message.
    memcpy(buf + bufPos, buffer, size);
    bufPos += size;

    // Add CRLF.
    buf[bufPos++] = '\r';
    buf[bufPos++] = '\n';

    Serial.write(buf, bufPos);
    if (lvl < INFO) {
        // Do not write debug to file.
        delete[] buf;
        return;
    }

    mutex_enter_blocking(&sdMu);
    _msgFile = sd.open(MESSAGE_LOG_PATH, FILE_WRITE);
    if (!_msgFile) {
        String msgDir = MESSAGE_LOG_PATH;
        msgDir.remove(msgDir.indexOf('/', 1));
        sd.mkdir(msgDir.c_str());
        _msgFile = sd.open(MESSAGE_LOG_PATH, FILE_WRITE);
    }
    if (_msgFile) {
        if (_restart) {
            _msgFile.write(PSTR("\r\n**** RESTART ****\r\n"));
            _restart = false;
        }

        _msgFile.write(buf, bufPos);
        _msgFile.close();
    }
    mutex_exit(&sdMu);

    delete[] buf;
}
