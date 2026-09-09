//
// Created by Nicholas Wiersma on 2026/09/09.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <HttpRequest.h>

#include "data_log.h"

// Drives an energy CSV export (one header row, then one row per interval
// covering [start, end]) as a c0Queue task writing into a held response,
// so a large export never blocks other core0 tasks. The only SD access
// (log.read()) happens from step(), never from start(), so queuing an
// export never blocks the caller (dispatch/lwIP callback) on SD I/O.
// Owns itself: deletes itself when the export completes or the client
// goes away.
//
// Usage from the route handler:
//     auto *job = EnergyExportJob::start(req, datalog, start, end, interval);
//     if (!job) return;                       // error/empty already sent
//     c0Queue.add(&EnergyExportJob::stepTask, 6, job);
class EnergyExportJob {
public:
    // Validates/clamps the parameters against the datalog (in-memory
    // fields only - no SD access) and parks the connection (req.hold).
    // Returns nullptr after sending an error/empty response if there is
    // nothing (or nothing valid) to export.
    static EnergyExportJob *start(HttpRequest &req, DataLog &log, uint32_t start, uint32_t end,
                                  uint32_t interval);

    // c0Queue trampoline.
    static uint32_t stepTask(void *param);

    ~EnergyExportJob();

private:
    EnergyExportJob(HttpRequest &req, DataLog &log) : _req(req), _log(log) {}

    uint32_t step();
    std::string buildHeader() const;

    struct DeviceColumn {
        uint8_t index;
        String name;
    };

    HttpRequest &_req;
    DataLog &_log;

    uint32_t _ts = 0;   // current row timestamp
    uint32_t _end = 0;
    uint32_t _interval = 0;
    bool _needsSeed = true;  // step() still needs to read _prevRec (first SD touch)

    std::vector<DeviceColumn> _columns;
    LogRecord _prevRec;
    std::string _pendingRow;  // a formatted row (or the header) that
                              // didn't fit the send window; retried whole
};
