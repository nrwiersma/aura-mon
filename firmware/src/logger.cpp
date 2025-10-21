//
// Created by Nicholas Wiersma on 2025/10/10.
//

#include "auramon.h"

const char* lvls[] PROGMEM = {"unkn", "dbug", "info", "eror"};

logger::logger() : _restart(true) {
    Serial.begin(115200);
}

void logger::errorf(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    writef(ERROR, format, arg);
    va_end(arg);
}

void logger::infof(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    writef(INFO, format, arg);
    va_end(arg);
}

void logger::debugf(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    writef(DEBUG, format, arg);
    va_end(arg);
}

void logger::writef(const LVL lvl, const char *format, va_list args) {
    char temp[64];
    char *buffer = temp;
    size_t len = vsnprintf(temp, sizeof(temp), format, args);
    if (len > sizeof(temp) - 1) {
        buffer = new char[len + 1];
        vsnprintf(buffer, len + 1, format, args);
    }
    write(lvl, buffer, len);
    if (buffer != temp) {
        delete[] buffer;
    }
}

void logger::write(const LVL lvl, const char *buffer, size_t size) {
    size_t bufSize = size+48;
    auto *buf = new char[bufSize];
    size_t bufPos = 0;

    time_t now;
    time(&now);
    if (now > 10000000) {
        // We have a system time, use it in the log.
        size_t len = strftime(buf, bufSize, PSTR("%Y-%m-%dT%H:%M:%SZ"), gmtime(&now));
        buf[len] = ' ';
        bufPos = len + 1;
    }

    // Write level.
    memcpy_P(buf + bufPos, lvls[lvl], strlen(lvls[lvl]));
    bufPos += strlen(lvls[lvl]);
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
