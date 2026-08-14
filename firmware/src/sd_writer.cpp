//
// Created by Nicholas Wiersma on 2026/08/13.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestLogger.h"
#include "sd_writer.h"
#endif

// Maximum number of jobs drained in a single task invocation. The SD writer
// shares core 0 with the web server, so it yields regularly rather than
// draining the whole queue at once.
constexpr size_t SD_DRAIN_BATCH = 4;

// Appends a formatted line to the message log. This is the single place the
// message log file is written, shared by the queued path and the synchronous
// path used under unit tests.
static void writeMessageLine(const char *buf, const size_t len) {
    mutex_enter_blocking(&sdMu);

    auto msgFile = sd.open(MESSAGE_LOG_PATH, FILE_WRITE);
    if (!msgFile) {
        String msgDir = MESSAGE_LOG_PATH;
        msgDir.remove(msgDir.indexOf('/', 1));
        sd.mkdir(msgDir.c_str());
        msgFile = sd.open(MESSAGE_LOG_PATH, FILE_WRITE);
    }
    if (msgFile) {
        msgFile.write(buf, len);
        msgFile.close();
    }

    mutex_exit(&sdMu);
}

#ifdef UNIT_TEST

// Native tests run without a task loop. Write through synchronously so the
// production file path is still exercised.
bool queueMessage(const char *buf, const size_t len) {
    writeMessageLine(buf, len);
    return true;
}

#else

namespace {
    struct msgSlot {
        uint16_t len;
        char     buf[SD_MSG_MAX];
    };

    mutex_t   recMu;
    LogRecord recQueue[SD_REC_QUEUE_LEN];
    size_t    recHead; // Next slot to read.
    size_t    recTail; // Next slot to write.
    size_t    recCount;

    mutex_t recMsgMu;
    msgSlot msgQueue[SD_MSG_QUEUE_LEN];
    size_t  msgHead;
    size_t  msgTail;
    size_t  msgCount;

    void publishDepth() {
        mutex_enter_blocking(&recMu);
        const size_t recs = recCount;
        mutex_exit(&recMu);

        mutex_enter_blocking(&recMsgMu);
        const size_t msgs = msgCount;
        mutex_exit(&recMsgMu);

        const uint32_t depth = recs + msgs;
        metrics.sd_queue_depth.store(depth, std::memory_order_relaxed);

        uint32_t high = metrics.sd_queue_high_water.load(std::memory_order_relaxed);
        while (depth > high &&
               !metrics.sd_queue_high_water.compare_exchange_weak(high, depth, std::memory_order_relaxed)) {
        }
    }

    // Moves one record out of the queue. Returns false when the queue is empty.
    bool popRecord(LogRecord *out) {
        mutex_enter_blocking(&recMu);
        if (recCount == 0) {
            mutex_exit(&recMu);
            return false;
        }
        *out = recQueue[recHead];
        recHead = (recHead + 1) % SD_REC_QUEUE_LEN;
        recCount--;
        mutex_exit(&recMu);
        return true;
    }

    bool popMessage(msgSlot *out) {
        mutex_enter_blocking(&recMsgMu);
        if (msgCount == 0) {
            mutex_exit(&recMsgMu);
            return false;
        }
        *out = msgQueue[msgHead];
        msgHead = (msgHead + 1) % SD_MSG_QUEUE_LEN;
        msgCount--;
        mutex_exit(&recMsgMu);
        return true;
    }

    // Writes a single queued job. Returns false when there was nothing to do.
    bool drainOne() {
        if (LogRecord rec; popRecord(&rec)) {
            const auto start = micros();
            if (auto err = datalog.write(&rec); err) {
                metrics.datalog_write_errors_total.fetch_add(1, std::memory_order_relaxed);
                LOGE("Error writing datalog: %s", err.Error());
                return true;
            }

            const uint32_t took = micros() - start;
            metrics.datalog_write_time_us_total.fetch_add(took, std::memory_order_relaxed);

            uint32_t max = metrics.datalog_write_time_us_max.load(std::memory_order_relaxed);
            while (took > max &&
                   !metrics.datalog_write_time_us_max.compare_exchange_weak(max, took, std::memory_order_relaxed)) {
            }
            return true;
        }

        if (msgSlot msg; popMessage(&msg)) {
            writeMessageLine(msg.buf, msg.len);
            return true;
        }

        return false;
    }
}

void initSDWriter() {
    mutex_init(&recMu);
    mutex_init(&recMsgMu);
}

bool queueLogRecord(const LogRecord *rec) {
    mutex_enter_blocking(&recMu);
    if (recCount == SD_REC_QUEUE_LEN) {
        // Never discard a datalog record; the caller retries instead. The
        // backlog is visible through sd_queue_high_water.
        mutex_exit(&recMu);
        return false;
    }
    recQueue[recTail] = *rec;
    recTail = (recTail + 1) % SD_REC_QUEUE_LEN;
    recCount++;
    mutex_exit(&recMu);

    publishDepth();
    return true;
}

bool queueMessage(const char *buf, size_t len) {
    if (len > SD_MSG_MAX) {
        len = SD_MSG_MAX;
    }

    bool dropped = false;

    mutex_enter_blocking(&recMsgMu);
    if (msgCount == SD_MSG_QUEUE_LEN) {
        // Discard the oldest line so the most recent context survives.
        msgHead = (msgHead + 1) % SD_MSG_QUEUE_LEN;
        msgCount--;
        dropped = true;
    }
    msgQueue[msgTail].len = static_cast<uint16_t>(len);
    memcpy(msgQueue[msgTail].buf, buf, len);
    msgTail = (msgTail + 1) % SD_MSG_QUEUE_LEN;
    msgCount++;
    mutex_exit(&recMsgMu);

    if (dropped) {
        metrics.sd_queue_dropped_total.fetch_add(1, std::memory_order_relaxed);
    }

    publishDepth();
    return !dropped;
}

uint32_t sdWriterTask(void *param) {
    (void) param;

    size_t done = 0;
    while (done < SD_DRAIN_BATCH && drainOne()) {
        done++;
    }

    publishDepth();

    // Come back immediately while there is a backlog, otherwise poll slowly.
    return done == SD_DRAIN_BATCH ? 1 : 20;
}

void drainSDWriter() {
    // Bounded so that a failing card, or an error path that logs while we
    // drain, cannot spin here forever.
    size_t guard = SD_REC_QUEUE_LEN + SD_MSG_QUEUE_LEN + SD_DRAIN_BATCH;
    while (guard-- > 0 && drainOne()) {
    }

    publishDepth();
}

#endif // UNIT_TEST
