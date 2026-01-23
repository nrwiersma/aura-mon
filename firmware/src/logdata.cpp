//
// Created by Nicholas Wiersma on 2026/01/23.
//

#include "auramon.h"

static uint32_t lastMS;

void initLogData() {
    lastMS = millis();
}

uint32_t logData(void *param) {
    (void) param;

    static bool   running;
    static auto * rec = new logRecord;
    static double hzHrs = 0;
    static double voltHrs[15] = {};
    static double wattHrs[15] = {};
    static double vaHrs[15] = {};
    const auto    start = millis();

    // If the clock is not running, try again later.
    if (!rtcRunning) {
        return 10;
    }

    if (!running) {
        if (datalog.entries()) {
            datalog.read(datalog.lastTS(), rec);
        }

        // Do not try and fill the gaps, just skip ahead.
        const auto now = time(nullptr);
        rec->ts = now;
        rec->ts -= rec->ts % datalog.interval();

        running = true;

        // We are early, come back.
        if (auto t = now % datalog.interval(); t > 0) {
            rec->ts += datalog.interval();
            return (datalog.interval() - t) * 1000;
        }
    }

    // If we are a little early, reschedule.
    if (time(nullptr) < rec->ts) return 2;

    const uint32_t nowMS = millis();
    const double   elapsedHrs = static_cast<double>(nowMS - lastMS) / MS_PER_HOUR;
    double         currHZHrs = 0;
    uint8_t        count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        const auto dev = devices[i];
        if (!dev || !dev->isEnabled()) {
            voltHrs[i] = 0;
            wattHrs[i] = 0;
            vaHrs[i] = 0;
            continue;
        }

        dev->accumulate(nowMS);
        rec->voltHrs[i] += dev->current.voltHrs - voltHrs[i];
        voltHrs[i] = dev->current.voltHrs;
        rec->wattHrs[i] += dev->current.wattHrs - wattHrs[i];
        wattHrs[i] = dev->current.wattHrs;
        rec->vaHrs[i] += dev->current.vaHrs - vaHrs[i];
        vaHrs[i] = dev->current.vaHrs;
        currHZHrs += dev->current.hzHrs;
        count++;
    }
    currHZHrs = currHZHrs / count;
    rec->hzHrs += currHZHrs - hzHrs;
    hzHrs = currHZHrs;

    lastMS = nowMS;
    rec->logHours += elapsedHrs;

    // Write the record.
    datalog.write(rec);

    const auto took = millis() - start;
    // TODO: log the stats.
    LOGD("Wrote record %d to log took %dms", rec->ts, took);

    rec->ts += datalog.interval();
    if (rec->ts < time(nullptr)) {
        // We are playing catchup, write at the next possible moment.
        return 1;
    }
    return datalog.interval() * 1000 - took;
}
