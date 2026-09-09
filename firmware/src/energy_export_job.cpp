//
// Created by Nicholas Wiersma on 2026/09/09.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif

#include <cmath>

#include "energy_export_job.h"

namespace {

void appendCSVValue(std::string &row, double value, int precision = 3) {
    row += ",";
    if (std::isfinite(value)) {
        row += String(value, precision).c_str();
    }
}

}  // namespace

EnergyExportJob *EnergyExportJob::start(HttpRequest &req, DataLog &log, uint32_t start, uint32_t end,
                                        uint32_t interval) {
    const uint32_t baseInterval = log.interval();
    start -= start % baseInterval;
    end -= end % baseInterval;
    interval -= interval % baseInterval;

    if (start >= end || interval == 0) {
        req.send(400, "text/plain", "invalid parameters");
        return nullptr;
    }
    if (end > start + interval * 99) {
        // Limit to 100 rows to keep responses a sane size.
        end = start + interval * 99;
    }

    if (!log.entries()) {
        req.send(204, "text/plain", "");
        return nullptr;
    }

    auto *job = new EnergyExportJob(req, log);

    mutex_enter_blocking(&deviceInfoMu);
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        auto info = deviceInfos[i];
        if (!info || !info->isEnabled() || info->name.isEmpty()) {
            continue;
        }
        job->_columns.push_back(DeviceColumn{i, info->name});
    }
    mutex_exit(&deviceInfoMu);

    if (job->_columns.empty()) {
        delete job;
        req.send(204, "text/plain", "");
        return nullptr;
    }

    const uint32_t lastTs = log.lastTS();
    if (start > lastTs) {
        delete job;
        req.send(204, "text/plain", "");
        return nullptr;
    }
    if (end > lastTs) {
        end = lastTs;
    }

    // Note: no log.read() here - that's the one call that actually touches
    // SD hardware, and it happens lazily on the first step() (queued, run
    // from c0Queue) so queuing an export never blocks the caller (which
    // runs inline from the request-dispatch/lwIP path) on SD I/O.
    job->_ts = start;
    job->_end = end;
    job->_interval = interval;

    req.hold(200, "text/plain");
    return job;
}

uint32_t EnergyExportJob::stepTask(void *param) {
    return static_cast<EnergyExportJob *>(param)->step();
}

EnergyExportJob::~EnergyExportJob() = default;

std::string EnergyExportJob::buildHeader() const {
    std::string header = "timestamp,Hz";
    for (const auto &col : _columns) {
        std::string name = col.name.c_str();
        header += "," + name + ".V";
        header += "," + name + ".A";
        header += "," + name + ".W";
        header += "," + name + ".Wh";
        header += "," + name + ".PF";
    }
    header += "\n";
    return header;
}

uint32_t EnergyExportJob::step() {
    if (!_req.alive()) {
        delete this;
        return 0;
    }

    if (_needsSeed) {
        // First SD touch for this export: seed _prevRec so the first row
        // can compute a delta. Runs here (from the queued task) rather
        // than from start(), so it never blocks the request-dispatch path.
        if (auto err = _log.read(_ts - _interval, &_prevRec); err) {
            static const char kErr[] = "error reading datalog\n";
            _req.write(reinterpret_cast<const uint8_t *>(kErr), sizeof(kErr) - 1);
            _req.done();
            delete this;
            return 0;
        }
        _needsSeed = false;
        _pendingRow = buildHeader();
    }

    // Flush a pending row (header or a row that didn't fit last time).
    if (!_pendingRow.empty()) {
        if (_req.write(reinterpret_cast<const uint8_t *>(_pendingRow.data()), _pendingRow.size()) == 0) {
            return 10;  // window still full; give it longer to drain
        }
        _pendingRow.clear();
    }

    while (_ts <= _end) {
        LogRecord rec;
        if (auto err = _log.read(_ts, &rec); err) {
            static const char kErr[] = "error reading datalog\n";
            _req.write(reinterpret_cast<const uint8_t *>(kErr), sizeof(kErr) - 1);
            _req.done();
            delete this;
            return 0;
        }

        const uint32_t thisTs = _ts;
        _ts += _interval;

        if (rec.ts <= _prevRec.ts || rec.rev == _prevRec.rev) {
            continue;
        }

        const double elapsedHours = rec.logHours - _prevRec.logHours;
        if (elapsedHours <= 0) {
            _prevRec = rec;
            continue;
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

        if (_req.write(reinterpret_cast<const uint8_t *>(row.data()), row.size()) == 0) {
            _pendingRow = std::move(row);
            return 10;  // window full; retry this row after a longer drain
        }
    }

    _req.done();
    delete this;
    return 0;
}
