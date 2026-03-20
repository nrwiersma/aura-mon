//
// Created by Nicholas Wiersma on 2025/09/05.

#pragma once

#include <errors.h>

// Total of 384 bytes.
struct LogRecord {
    uint32_t rev;
    uint32_t ts;       // Unix Timestamp
    double   logHours; // Total hours observed in this record.
    double   hzHrs;
    double   voltHrs[15];
    double   wattHrs[15];
    double   vaHrs[15];

    LogRecord() : rev(0),
                  ts(0),
                  logHours(0),
                  hzHrs(0),
                  voltHrs{},
                  wattHrs{},
                  vaHrs{} {
    };
};

class DataLog {
public:
    explicit DataLog(int interval = 5, double days = 180.0) : _interval(interval),
                                                              _recordSize(sizeof(LogRecord)),
                                                              _fileSize(0),
                                                              _maxFileSize(0),
                                                              _entries(0),
                                                              _first{},
                                                              _last{},
                                                              _wrapPos(0),
                                                              _lastReadTS(0),
                                                              _lastReadRev(0),
                                                              _lastCacheSize(60 / interval) {
        const double   recordsPerDay = 86400.0 / static_cast<double>(_interval);
        const uint32_t computedSize = static_cast<uint32_t>(days * recordsPerDay * _recordSize);
        _maxFileSize = max(static_cast<uint32_t>(_recordSize), computedSize);
        mutex_init(&_mu);
        _lastCache = new LogRecord[_lastCacheSize]{};
    };

    bool     begin();
    uint32_t entries();
    int      interval() const { return _interval; }
    uint32_t firstRev();
    uint32_t firstTS();
    uint32_t lastRev();
    uint32_t lastTS();
    uint32_t fileSize();
    Error *  read(uint32_t ts, LogRecord *rec, uint32_t timeoutMS = 100);
    Error *  write(LogRecord *rec);

private:
    struct LogRecordKey {
        uint32_t rev;
        uint32_t ts;
    };

    mutex_t _mu{};

    FsFile   _file;
    uint16_t _interval;
    uint16_t _recordSize;

    uint32_t     _fileSize;
    uint32_t     _maxFileSize;
    uint32_t     _entries;
    LogRecordKey _first;
    LogRecordKey _last;
    uint32_t     _wrapPos;

    uint32_t _lastReadTS;
    uint32_t _lastReadRev;

    uint32_t    _lastCacheSize;
    uint32_t    _lastCachePos = 0;
    LogRecord * _lastCache; // The last 60s of records.

    LogRecordKey readKey(uint32_t pos);
    uint8_t      readRev(uint32_t rev, LogRecord *rec);
    void         search(uint32_t ts, LogRecord *  rec,
                uint32_t         lowTS, uint32_t  lowRev,
                uint32_t         highTS, uint32_t highRev);
    uint32_t findWrapPos(uint32_t highPos, uint32_t highTS, uint32_t lowPos, uint32_t lowTS);
};
