//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <HttpResponseProducer.h>

#include "data_log.h"

// Streams the same energy CSV export as the previous blocking handler
// (one header row, then one row per interval covering [start, end]), but
// as a producer so a large export only ever holds datalog's own internal
// mutex for the duration of a single record lookup, and never blocks the
// connection loop while formatting rows.
class EnergyExportProducer : public HttpResponseProducer {
public:
    // start/end/interval are in seconds; end/interval should already have
    // their query-string defaults resolved by the caller (this class only
    // applies the datalog-interval alignment/clamping the original handler
    // performed).
    EnergyExportProducer(DataLog &log, uint32_t start, uint32_t end, uint32_t interval);

    Status produce(uint8_t *buf, size_t cap, size_t &written) override;

    int statusCode() const override { return _statusCode; }
    const char *contentType() const override { return "text/plain"; }
    const char *errorReason() const override { return _errorReason.empty() ? nullptr : _errorReason.c_str(); }

private:
    struct DeviceColumn {
        uint8_t index;
        String name;
    };

    enum class State {
        Start,
        FlushHeader,
        Row,
        FlushRow,
        Done,
        Error,
    };

    Status fail(int statusCode, const char *reason);
    // Copies as much of `_pending` (from `_pendingPos`) into `buf` as fits
    // in `cap`, returning true once every byte has been drained.
    bool drainPending(uint8_t *buf, size_t cap, size_t &written);

    DataLog &_log;

    State _state = State::Start;
    int _statusCode = 200;
    std::string _errorReason;

    uint32_t _start;
    uint32_t _end;
    uint32_t _interval;

    std::vector<DeviceColumn> _columns;
    LogRecord _prevRec;
    uint32_t _ts = 0;

    std::string _pending;
    size_t _pendingPos = 0;
    bool _finalAfterFlush = false;
};
