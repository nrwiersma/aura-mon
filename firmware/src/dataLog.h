//
// Created by Nicholas Wiersma on 2025/09/05.

#ifndef FIRMWARE_LOG_H
#define FIRMWARE_LOG_H

#ifdef UNIT_TEST
#include "../test/stubs/TestAuraMon.h"
#endif

// Total of 384 bytes.
struct logRecord {
    uint32_t rev;
    uint32_t ts;       // Unix Timestamp
    double   logHours; // Total hours observed in this record.
    double   hzHrs;
    double   voltHrs[15];
    double   wattHrs[15];
    double   vaHrs[15];

    logRecord() : rev(0),
                  ts(0),
                  logHours(0),
                  hzHrs(0),
                  voltHrs{},
                  wattHrs{},
                  vaHrs{} {
    };
};

class dataLog {
public:
    explicit dataLog(int interval = 5, int days = 180) : _interval(interval),
                                                         _recordSize(sizeof(logRecord)),
                                                         _fileSize(0),
                                                         _maxFileSize(days * _recordSize * (86400UL / interval)),
                                                         _entries(0),
                                                         _first{},
                                                         _last{},
                                                         _wrapPos(0),
                                                         _lastCacheSize(60 / interval),
                                                         _fileIO(0) {
        mutex_init(&_mu);
        _readCache = new logRecordKey[_readCacheSize];
        _lastCache = new logRecord[_lastCacheSize];
    };

    bool     begin();
    uint32_t entries();
    int      interval() const { return _interval; }
    uint32_t lastTS() const { return _last.ts; }
    int8_t   read(uint32_t ts, logRecord *rec, uint32_t timeoutMS = 100);
    int8_t   write(logRecord *rec);

private:
    struct logRecordKey {
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
    logRecordKey _first;
    logRecordKey _last;
    uint32_t     _wrapPos;

    uint32_t      _readCacheSize = 10;
    uint32_t      _readCachePos = 0;
    logRecordKey *_readCache; // The last 10 read keys.

    uint32_t   _lastCacheSize;
    uint32_t   _lastCachePos = 0;
    logRecord *_lastCache; // The last 60s of records.


    uint32_t _fileIO;

    logRecordKey readKey(uint32_t pos);
    uint8_t      readRev(uint32_t rev, logRecord *rec);
    void         search(uint32_t ts, logRecord * rec,
                uint32_t         lowTS, int32_t  lowRev,
                uint32_t         highTS, int32_t highRev);
    uint32_t findWrapPos(uint32_t highPos, uint32_t highTS, uint32_t lowPos, uint32_t lowTS);
};

void        initLogData();
const char *readError(uint32_t err);

#endif //FIRMWARE_LOG_H
