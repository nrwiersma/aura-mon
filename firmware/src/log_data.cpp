//
// Created by Nicholas Wiersma on 2026/01/23.
//

#include "auramon.h"

// The number of times a record write is retried before the record is dropped.
#define MAX_WRITE_RETRIES 5

// How long the writer waits before checking the queue again when it is empty.
#define WRITE_IDLE_MS 100

// The longest the queue is drained for on shutdown.
#define DRAIN_TIMEOUT_MS 500

static uint32_t lastMS;

static logQueue records;

static bool writeNextRecord();

void initLogData() {
    lastMS = millis();
}

uint32_t logData(void *param) {
    (void) param;

    static bool   running;
    static auto * rec = new LogRecord;
    static double hzHrs = 0;
    static double voltHrs[15] = {};
    static double wattHrs[15] = {};
    static double vaHrs[15] = {};
    // Hang detection state: when the current queue backup started (0 = queue
    // not backed up) and the last observed queue depth, used to tell a wedged
    // consumer apart from one that is merely slow.
    static uint32_t queueFullSinceMs = 0;
    static uint32_t queueLastDepth = 0;
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
    if (count > 0) {
        currHZHrs = currHZHrs / count;
    }
    rec->hzHrs += currHZHrs - hzHrs;
    hzHrs = currHZHrs;

    lastMS = nowMS;
    rec->logHours += elapsedHrs;

    // Queue the record, it is written to the data log on the other core.
    // It is safe to keep using `rec` and the queue copies the record.
    if (!records.push(rec)) {
        metrics.datalog_queue_full_total.fetch_add(1, std::memory_order_relaxed);
        LOGE("Log record queue is full, retrying record %u", rec->ts);

        // A full queue only means core 0 is wedged if it is also making no
        // progress draining it. Track when the current backup started and
        // restart the window whenever the consumer drains a record, so that
        // slow-but-alive SD writes (e.g. a long FAT flush) and catch-up
        // bursts do not trigger a false restart.
        const auto depth = records.size();
        const auto now = millis();
        if (queueFullSinceMs == 0 || depth < queueLastDepth) {
            queueFullSinceMs = now;
        }
        queueLastDepth = depth;

        if (now - queueFullSinceMs >= QUEUE_FULL_WATCHDOG_MS) {
            // The queue has been full for a sustained period with no
            // progress, so the consumer on core 0 is likely wedged. Force an
            // immediate watchdog restart.
            LOGE("Log write queue stuck full with no progress for %lums; forcing a watchdog restart",
                 static_cast<unsigned long>(now - queueFullSinceMs));

            uint32_t taskAddr = 0, taskStartMs = 0;
            const auto stuckMs = c0Queue.currentTask(&taskAddr, &taskStartMs) ? now - taskStartMs : 0;
            forceCore0HangReboot(taskAddr, stuckMs);
        }
        return 10;
    }
    queueFullSinceMs = 0;
    metrics.datalog_queue_depth.store(records.size(), std::memory_order_relaxed);

    const auto took = millis() - start;
    LOGD("Queued record %d for log took %dms", rec->ts, took);

    rec->ts += datalog.interval();
    if (rec->ts < time(nullptr)) {
        // We are playing catchup, write at the next possible moment.
        return 1;
    }
    return datalog.interval() * 1000 - took;
}

uint32_t writeLogData(void *param) {
    (void) param;

    // Nothing to do, come back later.
    if (!writeNextRecord()) {
        return WRITE_IDLE_MS;
    }

    // There may be more records waiting, come back as soon as we can.
    return 1;
}

void drainLogData() {
    const auto start = millis();

    while (millis() - start < DRAIN_TIMEOUT_MS) {
        if (!writeNextRecord()) {
            return;
        }
    }
}

// writeNextRecord writes the record at the front of the queue to the data log,
// returning false if there is no record to write.
//
// A record that fails to write is held onto and retried, as the data log
// requires records to be written in order. Once the retries are exhausted the
// record is dropped, so a single bad record cannot block the queue forever.
static bool writeNextRecord() {
    static LogRecord rec;
    static bool      pending;
    static uint8_t   retries;

    if (!pending) {
        if (!records.pop(&rec)) {
            return false;
        }
        metrics.datalog_queue_depth.store(records.size(), std::memory_order_relaxed);

        pending = true;
        retries = 0;
    }

    const auto start = millis();
    if (auto err = datalog.write(&rec); err) {
        metrics.datalog_write_errors_total.fetch_add(1, std::memory_order_relaxed);
        metrics.datalog_write_consecutive_failures.fetch_add(1, std::memory_order_relaxed);
        LOGE("Error writing datalog: %s", err.Error());

        if (++retries < MAX_WRITE_RETRIES) {
            return true;
        }

        pending = false;
        metrics.datalog_records_dropped_total.fetch_add(1, std::memory_order_relaxed);
        LOGE("Dropped record %d after %d failed writes", rec.ts, retries);
        return true;
    }
    pending = false;
    metrics.datalog_write_consecutive_failures.store(0, std::memory_order_relaxed);

    const auto took = millis() - start;
    metrics.datalog_write_time_ms_total.fetch_add(took, std::memory_order_relaxed);
    LOGD("Wrote record %d to log took %dms", rec.ts, took);

    return true;
}
