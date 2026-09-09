//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <HttpResponseProducer.h>

// Streams a byte range of the message log, one bounded chunk at a time, so
// a large export never holds the SD card (or the connection) for longer
// than a single read.
class LogRangeProducer : public HttpResponseProducer {
public:
    LogRangeProducer(uint32_t startOffset, uint32_t limitBytes);

    Status produce(uint8_t *buf, size_t cap, size_t &written) override;

    int statusCode() const override { return _statusCode; }
    const char *contentType() const override { return "text/plain"; }
    const char *errorReason() const override { return _errorReason.empty() ? nullptr : _errorReason.c_str(); }

private:
    enum class State {
        Start,
        Copy,
        Done,
        Error,
    };

    Status fail(int statusCode, const char *reason);

    State _state = State::Start;
    int _statusCode = 200;
    std::string _errorReason;

    uint32_t _startOffset;
    uint32_t _limitBytes;
    size_t _remaining = 0;

    FsFile _src;
};
