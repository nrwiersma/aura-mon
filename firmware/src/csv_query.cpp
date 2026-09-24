//
// Created by Nicholas Wiersma on 2026/09/11.
//

#include "csv_query.h"

void appendCSVValue(String &row, double value, const uint8_t precision = 3) {
    row += ",";
    if (std::isfinite(value)) {
        row += String(value, precision);
    }
}

uint32_t csvQuery::_step() {
    if (_start >= _end) {
        if (_headerSent) {
            // We've already streamed a 200 response (and possibly some rows); the
            // time-slice yield just landed exactly on the last timestamp. There's
            // no more data to send, so simply terminate the chunked response
            // rather than writing a fresh status line into the open stream.
            _req->finish();
        } else {
            _req->send(204, F("text/plain"), F(""));
            _req->finish();
        }
        return 0;
    }

    if (!_headerSent) {
        if (!_previousRecordRead) {
            if (auto err = datalog.read(_start - _interval, &_prevRec); err) {
                _req->send(500, F("application/json"), F("{\"error\":\"Unable to read datalog\"}"));
                _req->finish();
                return 0;
            }
            _previousRecordRead = true;
        }

        _fetchDeviceColumns();
        if (_deviceCount == 0) {
            _req->send(204, F("text/plain"), F(""));
            _req->finish();
            return 0;
        }

        String header = F("timestamp,Hz");
        for (size_t i = 0; i < _deviceCount; i++) {
            const String &name = _deviceColumns[i].name;
            header += "," + name + ".V";
            header += "," + name + ".A";
            header += "," + name + ".W";
            header += "," + name + ".Wh";
            header += "," + name + ".PF";
        }
        header += "\n";

        _req->sendHeader("Cache-Control", "no-cache");
        _req->send(200, "text/csv", header);
        _headerSent = true;
    }

    auto start = millis();

    for (uint32_t ts = _start; ts <= _end; ts += _interval) {
        LogRecord rec;
        if (auto err = datalog.read(ts, &rec); err) {
            _req->sendContent(F("error reading datalog\n"));
            _req->finish();
            return 0;
        }

        if (rec.ts <= _prevRec.ts) {
            continue;
        }
        if (rec.rev == _prevRec.rev) {
            continue;
        }

        const double elapsedHours = rec.logHours - _prevRec.logHours;
        if (elapsedHours <= 0) {
            _prevRec = rec;
            continue;
        }

        auto row = String(rec.ts);
        row.reserve(row.length() + _deviceCount * 48);

        const double hz = (rec.hzHrs - _prevRec.hzHrs) / elapsedHours;
        appendCSVValue(row, hz, 2);

        for (size_t i = 0; i < _deviceCount; i++) {
            const uint8_t idx = _deviceColumns[i].index;
            const double  voltage = (rec.voltHrs[idx] - _prevRec.voltHrs[idx]) / elapsedHours;
            double        energyWh = rec.wattHrs[idx] - _prevRec.wattHrs[idx];
            const double  power = energyWh / elapsedHours;
            const double  apparentPower = (rec.vaHrs[idx] - _prevRec.vaHrs[idx]) / elapsedHours;
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
        _req->sendContent(row);
        _prevRec = rec;

        if (millis() - start > 100) {
            // If we've been processing for more than 100ms, yield to avoid blocking.
            _start = ts;
            return 10;
        }
    }

    // All records where returned.
    _req->finish();
    return 0;
}

void csvQuery::_fetchDeviceColumns() {
    mutex_enter_blocking(&deviceInfoMu);
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        auto info = deviceInfos[i];
        if (!info || !info->isEnabled() || info->name.isEmpty()) {
            continue;
        }
        _deviceColumns[_deviceCount++] = deviceColumn{i, info->name};
    }
    mutex_exit(&deviceInfoMu);
}
