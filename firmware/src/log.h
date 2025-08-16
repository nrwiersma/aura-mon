//
// Created by Nicholas Wiersma on 2025/09/05.

#ifndef FIRMWARE_LOG_H
#define FIRMWARE_LOG_H

#include "auramon.h"

// Total of 504 bytes.
struct logRecord {
    uint32_t rev;
    uint32_t ts;        // Unix Timestamp
    double logHours;    // Total hours observed in this record.
    double hzHrs;
    double voltHrs[20];
    double wattHrs[20];
    double vaHrs[20];

    logRecord() : rev(0), ts(0) {
    };
};

#endif //FIRMWARE_LOG_H
