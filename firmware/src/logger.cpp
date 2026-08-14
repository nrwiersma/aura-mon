//
// Created by Nicholas Wiersma on 2025/10/10.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestLogger.h"
#include "logger.h"
#include "storage.h"
#endif

namespace {
    // Longer message log lines are truncated. The line is copied into the
    // queued closure, so this bounds the queue entry size.
    constexpr size_t messageMaxLen = 192;

    // Queues a line to be appended to the message log. A full queue drops the
    // newest line rather than the oldest, as during a burst the earliest lines
    // carry the cause.
    void queueMessage(const char *buf, size_t len) {
        if (len > messageMaxLen) {
            len = messageMaxLen;
        }

        struct msgJob {
            uint16_t len;
            char     buf[messageMaxLen];
        } job{static_cast<uint16_t>(len), {}};
        memcpy(job.buf, buf, len);

        if (!storage::submit([job](storage::sdAccess &sd) -> error {
            return sd.append(MESSAGE_LOG_PATH, job.buf, job.len);
        })) {
            metrics.sd_queue_dropped_total.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

const char *lvls[] PROGMEM = {"unkn", "dbug", "info", "eror"};

Logger::Logger() : _restart(true) {
    Serial.begin(115200);
}

void Logger::printf(const LVL lvl, const char *format, ...) {
    char    temp[64];
    int     n;

    va_list arg;

    va_start(arg, format);
    n = vsnprintf(temp, sizeof(temp), format, arg);

    va_end(arg);

    if (n < 0) return;

    char*  buffer = temp;
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

    write(lvl, buffer, len);

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
    if (lvl == LOG_LEVEL_ERROR) {
        metrics.log_errors_total.fetch_add(1, std::memory_order_relaxed);
    }
    if (lvl < LOG_LEVEL_INFO) {
        // Do not write debug to file.
        delete[] buf;
        return;
    }

    if (_restart) {
        static const char marker[] = "\r\n**** RESTART ****\r\n";
        queueMessage(marker, sizeof(marker) - 1);
        _restart = false;
    }

    queueMessage(buf, bufPos);

    delete[] buf;
}