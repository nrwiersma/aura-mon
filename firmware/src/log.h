//
// Created by Nicholas Wiersma on 2025/09/05.

#ifndef FIRMWARE_LOG_H
#define FIRMWARE_LOG_H

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

class log {
public:
    log();

    int8_t begin(const char *path);
    int8_t read(logRecord *rec);
    int8_t read(logRecord *rec, size_t size);
    int8_t write(logRecord *rec);

private:
    FsFile      _file;
    uint16_t    _interval;
    uint16_t    _recordSize;

    uint32_t _fileSize;
    uint32_t _physicalSize;
};

#endif //FIRMWARE_LOG_H
