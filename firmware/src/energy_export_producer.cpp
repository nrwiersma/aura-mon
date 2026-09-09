//
// Created by Nicholas Wiersma on 2026/01/23.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "energy_export_producer.h"
#endif

#include <cmath>
#include <cstring>

#include "energy_export_producer.h"

namespace {

void appendCSVValue(std::string &row, double value, int precision = 3) {
    row += ",";
    if (std::isfinite(value)) {
        row += String(value, precision).c_str();
    }
}

}  // namespace

EnergyExportProducer::EnergyExportProducer(DataLog &log, uint32_t start, uint32_t end, uint32_t interval)
    : _log(log), _start(start), _end(end), _interval(interval) {}

HttpResponseProducer::Status EnergyExportProducer::fail(int statusCode, const char *reason) {
    _statusCode = statusCode;
    _errorReason = reason;
    _state = State::Error;
    return Status::Error;
}

bool EnergyExportProducer::drainPending(uint8_t *buf, size_t cap, size_t &written) {
    const size_t remaining = _pending.size() - _pendingPos;
    const size_t n = min(remaining, cap);
    if (n > 0) {
        memcpy(buf, _pending.data() + _pendingPos, n);
        _pendingPos += n;
    }
    written = n;
    if (_pendingPos >= _pending.size()) {
        _pending.clear();
        _pendingPos = 0;
        return true;
    }
    return false;
}

HttpResponseProducer::Status EnergyExportProducer::produce(uint8_t *buf, size_t cap, size_t &written) {
    written = 0;

    switch (_state) {
        case State::Start: {
            const uint32_t baseInterval = _log.interval();

            _start -= _start % baseInterval;
            _end -= _end % baseInterval;
            _interval -= _interval % baseInterval;

            if (_start >= _end || _interval == 0) {
                return fail(400, "invalid parameters");
            }
            if (_end > _start + _interval * 99) {
                // Limit to 100 rows to prevent excessively large responses.
                _end = _start + _interval * 99;
            }

            if (!_log.entries()) {
                _statusCode = 204;
                _state = State::Done;
                return Status::Done;
            }

            mutex_enter_blocking(&deviceInfoMu);
            for (uint8_t i = 0; i < MAX_DEVICES; i++) {
                auto info = deviceInfos[i];
                if (!info || !info->isEnabled() || info->name.isEmpty()) {
                    continue;
                }
                _columns.push_back(DeviceColumn{i, info->name});
            }
            mutex_exit(&deviceInfoMu);

            if (_columns.empty()) {
                _statusCode = 204;
                _state = State::Done;
                return Status::Done;
            }

            const uint32_t lastTs = _log.lastTS();
            if (_start > lastTs) {
                _statusCode = 204;
                _state = State::Done;
                return Status::Done;
            }
            if (_end > lastTs) {
                _end = lastTs;
            }

            if (auto err = _log.read(_start - _interval, &_prevRec); err) {
                return fail(500, err.Error());
            }

            _pending = "timestamp,Hz";
            for (const auto &col : _columns) {
                std::string name = col.name.c_str();
                _pending += "," + name + ".V";
                _pending += "," + name + ".A";
                _pending += "," + name + ".W";
                _pending += "," + name + ".Wh";
                _pending += "," + name + ".PF";
            }
            _pending += "\n";

            _ts = _start;
            _state = State::FlushHeader;
            return Status::Pending;
        }

        case State::FlushHeader: {
            const bool flushed = drainPending(buf, cap, written);
            if (flushed) {
                _state = State::Row;
            }
            return (written > 0) ? Status::Data : Status::Pending;
        }

        case State::Row: {
            if (_ts > _end) {
                _state = State::Done;
                return Status::Done;
            }

            LogRecord rec;
            if (auto err = _log.read(_ts, &rec); err) {
                _pending = "error reading datalog\n";
                _pendingPos = 0;
                _finalAfterFlush = true;
                _state = State::FlushRow;
                return Status::Pending;
            }

            if (rec.ts <= _prevRec.ts || rec.rev == _prevRec.rev) {
                _ts += _interval;
                return Status::Pending;
            }

            const double elapsedHours = rec.logHours - _prevRec.logHours;
            if (elapsedHours <= 0) {
                _prevRec = rec;
                _ts += _interval;
                return Status::Pending;
            }

            std::string row = std::to_string(rec.ts);

            const double hz = (rec.hzHrs - _prevRec.hzHrs) / elapsedHours;
            appendCSVValue(row, hz, 2);

            for (const auto &col : _columns) {
                const uint8_t idx = col.index;
                const double voltage = (rec.voltHrs[idx] - _prevRec.voltHrs[idx]) / elapsedHours;
                double energyWh = rec.wattHrs[idx] - _prevRec.wattHrs[idx];
                const double power = energyWh / elapsedHours;
                const double apparentPower = (rec.vaHrs[idx] - _prevRec.vaHrs[idx]) / elapsedHours;
                if (energyWh < 0) {
                    energyWh = 0;
                }
                const double current = (voltage != 0.0) ? (apparentPower / voltage) : 0.0;
                const double powerFactor = (apparentPower > 0.0) ? (power / apparentPower) : 0.0;

                appendCSVValue(row, voltage);
                appendCSVValue(row, current);
                appendCSVValue(row, power);
                appendCSVValue(row, energyWh, 6);
                appendCSVValue(row, powerFactor, 4);
            }

            row += "\n";
            _prevRec = rec;
            _ts += _interval;

            _pending = row;
            _pendingPos = 0;
            _finalAfterFlush = false;
            _state = State::FlushRow;
            return Status::Pending;
        }

        case State::FlushRow: {
            const bool flushed = drainPending(buf, cap, written);
            if (flushed) {
                _state = _finalAfterFlush ? State::Done : State::Row;
                if (_finalAfterFlush) {
                    return Status::Done;
                }
            }
            return Status::Data;
        }

        case State::Done:
        case State::Error:
        default:
            return Status::Done;
    }
}
