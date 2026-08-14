//
// Created by Nicholas Wiersma on 2026/08/13.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "storage.h"
#endif

#include <cstring>

#ifndef UNIT_TEST
// The card and its lock are private to this module: every path to them runs
// through a transaction, so nothing can reach the card without the lock.
namespace {
    mutex_t sdMu;
    SdFs    sd;
}
#endif

namespace {
    // Prefixes every queued job. A null invoke marks unusable space at the end
    // of the queue that the reader must skip over.
    struct jobHeader {
        uint32_t                  len; // Total entry length, including header.
        uint32_t                  queuedAt;
        storage::detail::invokeFn invoke;
    };

    constexpr size_t alignUp(const size_t n) {
        return (n + storage::queueAlign - 1) & ~(storage::queueAlign - 1);
    }

    constexpr size_t headerSize = alignUp(sizeof(jobHeader));

    static_assert(storage::queueBytes % storage::queueAlign == 0,
                  "queue size must be a whole number of alignment units");
    static_assert(headerSize < storage::jobMax, "job header does not fit in a job");

    alignas(storage::queueAlign) uint8_t queueBuf[storage::queueBytes];

    mutex_t queueMu;
    size_t  queueHead; // Next byte to read.
    size_t  queueTail; // Next byte to write.
    size_t  queueUsed; // Bytes between head and tail, padding included.
    size_t  queueJobs;

    // Tracked per core: a nested transaction on the same core would deadlock on
    // the non-recursive card lock, but a transaction on the other core must
    // simply wait.
    bool inTx[2];

    constexpr size_t pinnedSlots = 3;
    constexpr size_t pathMaxLen = 96;

    // Only pinned paths keep a handle open between calls. DataLog binary
    // searches the log file, so reopening on every seek would be ruinous.
    // Everything else opens and closes within a single primitive, so no handle
    // can outlive the call and refer to a file that has since been replaced.
    struct pinnedFile {
        char   path[pathMaxLen];
        FsFile file;
    };

    pinnedFile pinned[pinnedSlots];

    bool samePath(const char *a, const char *b) {
        return strncmp(a, b, pathMaxLen) == 0;
    }

    void closePinned(pinnedFile &slot) {
        if (slot.file) {
            slot.file.flush();
            slot.file.close();
        }
        slot.path[0] = '\0';
    }

    void evictPath(const char *path) {
        for (auto &slot : pinned) {
            if (slot.path[0] != '\0' && samePath(slot.path, path)) {
                closePinned(slot);
            }
        }
    }

    // Borrows a handle for path, reusing the pinned one where there is one and
    // otherwise opening a handle that closes when the reference goes out of
    // scope. Converts to false if the file could not be opened.
    class fileRef {
    public:
        fileRef(const char *path, const oflag_t oflag) {
            if (strnlen(path, pathMaxLen) >= pathMaxLen) {
                return;
            }

            for (auto &slot : pinned) {
                if (slot.path[0] != '\0' && samePath(slot.path, path)) {
                    _file = &slot.file;
                    return;
                }
            }

            _own = sd.open(path, oflag);
            if (_own) {
                _file = &_own;
            }
        }

        ~fileRef() {
            if (_own) {
                _own.flush();
                _own.close();
            }
        }

        fileRef(const fileRef &) = delete;
        fileRef &operator=(const fileRef &) = delete;

        explicit operator bool() const { return _file != nullptr; }
        FsFile *operator->() const { return _file; }
        FsFile *get() const { return _file; }

    private:
        FsFile  _own;
        FsFile *_file = nullptr;
    };

    error writeEnd(const fileRef &f, const void *buf, const size_t len) {
        if (!f->seek(f->size())) {
            return newError("could not seek file");
        }
        if (f->write(buf, len) != len) {
            return newError("short write");
        }
        f->flush();
        return {};
    }

    unsigned currentCore() {
#ifdef UNIT_TEST
        return 0;
#else
        return get_core_num();
#endif
    }

    void publishLatency(const uint32_t queuedAt) {
        const uint32_t waited = micros() - queuedAt;

        uint32_t longest = metrics.sd_queue_latency_us_max.load(std::memory_order_relaxed);
        while (waited > longest &&
               !metrics.sd_queue_latency_us_max.compare_exchange_weak(longest, waited, std::memory_order_relaxed)) {
        }
    }

    void publishDepth() {
        metrics.sd_queue_depth.store(queueJobs, std::memory_order_relaxed);

        uint32_t high = metrics.sd_queue_high_water.load(std::memory_order_relaxed);
        while (queueJobs > high &&
               !metrics.sd_queue_high_water.compare_exchange_weak(high, queueJobs, std::memory_order_relaxed)) {
        }
    }

    // Copies the next job out of the queue, returning false when there is
    // nothing to run. The job is copied rather than run in place so the queue
    // lock is released before it executes, which is what allows a job to submit
    // more work.
    bool popJob(storage::detail::invokeFn *fn, void *out, const size_t outLen) {
        mutex_enter_blocking(&queueMu);

        while (queueUsed > 0) {
            // The writer wrapped past a gap too small to hold a skip marker.
            if (storage::queueBytes - queueHead < headerSize) {
                queueUsed -= storage::queueBytes - queueHead;
                queueHead = 0;
                continue;
            }

            const auto *hdr = reinterpret_cast<const jobHeader *>(queueBuf + queueHead);
            if (hdr->invoke == nullptr) {
                queueUsed -= hdr->len;
                queueHead = 0;
                continue;
            }

            const size_t payload = hdr->len - headerSize;
            if (payload > outLen) {
                // Unreachable while pushJob rejects oversized closures. Drop the
                // entry rather than overflow the caller's buffer.
                queueUsed -= hdr->len;
                queueHead = (queueHead + hdr->len) % storage::queueBytes;
                queueJobs--;
                mutex_exit(&queueMu);
                return false;
            }

            *fn = hdr->invoke;
            const uint32_t queuedAt = hdr->queuedAt;
            memcpy(out, queueBuf + queueHead + headerSize, payload);

            queueUsed -= hdr->len;
            queueHead = (queueHead + hdr->len) % storage::queueBytes;
            queueJobs--;

            mutex_exit(&queueMu);

            publishLatency(queuedAt);
            return true;
        }

        mutex_exit(&queueMu);
        return false;
    }

    // Runs a single queued job under the card lock, returning false when there
    // was nothing to do. The card is claimed before the job is taken off the
    // queue, so a lock timeout leaves the job queued rather than dropping it.
    bool drainOne() {
        if (!storage::detail::lockEnter(SD_LOCK_TIMEOUT_MS)) {
            return false;
        }

        alignas(storage::queueAlign) uint8_t job[storage::jobMax - headerSize];

        storage::detail::invokeFn fn = nullptr;
        if (!popJob(&fn, job, sizeof(job))) {
            storage::detail::lockExit();
            publishDepth();
            return false;
        }

        storage::sdAccess sd = storage::detail::accessFactory::make();
        if (auto err = fn(job, sd); err) {
            metrics.sd_job_errors_total.fetch_add(1, std::memory_order_relaxed);
        }

        storage::detail::lockExit();

        publishDepth();
        return true;
    }
}

namespace storage {
    error sdAccess::readAt(const char *path, const uint32_t pos, void *buf, const size_t len) {
        const fileRef f(path, O_RDONLY);
        if (!f) {
            return newError("could not open file");
        }
        if (!f->seek(pos)) {
            return newError("could not seek file");
        }
        if (static_cast<size_t>(f->read(buf, len)) != len) {
            return newError("short read");
        }

        metrics.datalog_read_io.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    error sdAccess::writeAt(const char *path, const uint32_t pos, const void *buf, const size_t len) {
        const fileRef f(path, O_RDWR | O_CREAT);
        if (!f) {
            return newError("could not open file");
        }
        if (!f->seek(pos)) {
            return newError("could not seek file");
        }
        if (f->write(buf, len) != len) {
            return newError("short write");
        }
        f->flush();
        return {};
    }

    error sdAccess::append(const char *path, const void *buf, const size_t len) {
        if (const fileRef f(path, O_RDWR | O_CREAT); f) {
            return writeEnd(f, buf, len);
        }

        // The parent directory may not exist yet.
        if (auto err = mkdirFor(path); err) {
            return err;
        }

        const fileRef f(path, O_RDWR | O_CREAT);
        if (!f) {
            return newError("could not open file");
        }
        return writeEnd(f, buf, len);
    }

    error sdAccess::readJSON(const char *path, JsonDocument &doc) {
        const fileRef f(path, O_RDONLY);
        if (!f) {
            return newError("could not open file");
        }
        // A pinned handle carries the position left by the last caller.
        if (!f->seek(0)) {
            return newError("could not seek file");
        }
        if (deserializeJson(doc, *f.get())) {
            return newError("could not decode file");
        }
        return {};
    }

    error sdAccess::writeJSON(const char *path, const char *tmpPath, const JsonDocument &doc) {
        if (auto err = mkdirFor(path); err) {
            return err;
        }

        {
            const fileRef tmp(tmpPath, O_RDWR | O_CREAT);
            if (!tmp) {
                return newError("could not create file");
            }
            // truncate() cuts at the current position, so this must seek first
            // or an existing longer file keeps its tail.
            if (!tmp->seek(0)) {
                return newError("could not seek file");
            }
            tmp->truncate();
            if (serializeJson(doc, *tmp.get()) == 0) {
                return newError("could not write file");
            }
        }

        // Only now is the temp file complete, so replacing the target cannot
        // lose the previous contents. Going through the primitives keeps the
        // eviction invariant: no pinned handle survives a remove or rename.
        this->remove(path);
        return this->rename(tmpPath, path);
    }

    uint32_t sdAccess::size(const char *path) {
        if (!sd.exists(path)) {
            return 0;
        }
        const fileRef f(path, O_RDONLY);
        if (!f) {
            return 0;
        }
        return f->size();
    }

    bool sdAccess::exists(const char *path) {
        return sd.exists(path);
    }

    bool sdAccess::isDirectory(const char *path) {
        const fileRef f(path, O_RDONLY);
        if (!f) {
            return false;
        }
        return f->isDirectory();
    }

    error sdAccess::remove(const char *path) {
        // The pinned handle must go first, or it keeps referring to a file that
        // no longer exists.
        evictPath(path);

        if (!sd.remove(path)) {
            return newError("could not remove file");
        }
        return {};
    }

    error sdAccess::rename(const char *from, const char *to) {
        evictPath(from);
        evictPath(to);

        if (!sd.rename(from, to)) {
            return newError("could not rename file");
        }
        return {};
    }

    error sdAccess::mkdirFor(const char *path) {
        const char *slash = strrchr(path, '/');
        if (slash == nullptr || slash == path) {
            return {};
        }

        const size_t len = static_cast<size_t>(slash - path);
        if (len >= pathMaxLen) {
            return newError("path too long");
        }

        char dir[pathMaxLen];
        memcpy(dir, path, len);
        dir[len] = '\0';

        if (sd.exists(dir)) {
            return {};
        }
        if (!sd.mkdir(dir)) {
            return newError("could not create directory");
        }
        return {};
    }

    void sdAccess::pin(const char *path) {
        if (strnlen(path, pathMaxLen) >= pathMaxLen) {
            return;
        }
        for (const auto &slot : pinned) {
            if (slot.path[0] != '\0' && samePath(slot.path, path)) {
                return;
            }
        }

        for (auto &slot : pinned) {
            if (slot.path[0] != '\0') {
                continue;
            }

            slot.file = sd.open(path, O_RDWR | O_CREAT);
            if (!slot.file) {
                slot.file = sd.open(path, O_RDONLY);
            }
            if (!slot.file) {
                return;
            }
            strncpy(slot.path, path, pathMaxLen - 1);
            slot.path[pathMaxLen - 1] = '\0';
            return;
        }
    }

    void sdAccess::unpin(const char *path) {
        evictPath(path);
    }

    void sdAccess::closeAll() {
        for (auto &slot : pinned) {
            closePinned(slot);
        }
    }

    void init() {
        mutex_init(&queueMu);
#ifndef UNIT_TEST
        mutex_init(&sdMu);
#endif

        for (auto &slot : pinned) {
            slot.path[0] = '\0';
            slot.file = FsFile{};
        }

        queueHead = 0;
        queueTail = 0;
        queueUsed = 0;
        queueJobs = 0;
    }

    bool inTransaction() {
        return inTx[currentCore()];
    }

#ifndef UNIT_TEST
    bool begin() {
        if (sd.begin(SD_CONFIG)) {
            return true;
        }

        Serial.println("Could not initialize SD Card. Halting");
        sd.initErrorPrint(&Serial);
        return false;
    }

    void end() {
        // Never released: no further card access may start once the peripheral
        // is torn down.
        mutex_enter_blocking(&sdMu);
        sd.end();
    }

    bool status(uint8_t &errorCode) {
        mutex_enter_blocking(&sdMu);
        const uint32_t cardStatus = sd.card()->status();
        errorCode = sd.card()->errorCode();
        mutex_exit(&sdMu);

        return cardStatus != 0 && errorCode == 0;
    }
#endif

    namespace detail {
        bool lockEnter(const uint32_t timeoutMS) {
            const unsigned core = currentCore();
            if (inTx[core]) {
                return false;
            }

            if (!mutex_enter_timeout_ms(&sdMu, timeoutMS)) {
                return false;
            }

            inTx[core] = true;
            return true;
        }

        void lockExit() {
            inTx[currentCore()] = false;
            mutex_exit(&sdMu);
        }

        bool pushJob(const invokeFn fn, const void *closure, const size_t len) {
            const size_t need = alignUp(headerSize + len);
            if (need > jobMax) {
                return false;
            }

            mutex_enter_blocking(&queueMu);

            // Wrap when the entry will not fit contiguously before the end.
            const size_t toEnd = queueBytes - queueTail;
            const size_t pad = toEnd < need ? toEnd : 0;
            if (queueBytes - queueUsed < pad + need) {
                mutex_exit(&queueMu);
                return false;
            }

            if (pad > 0) {
                if (pad >= headerSize) {
                    // Mark the gap so the reader skips it. A smaller gap is
                    // detected by position instead.
                    auto *skip = reinterpret_cast<jobHeader *>(queueBuf + queueTail);
                    skip->len = static_cast<uint32_t>(pad);
                    skip->invoke = nullptr;
                }
                queueTail = 0;
            }

            auto *hdr = reinterpret_cast<jobHeader *>(queueBuf + queueTail);
            hdr->len = static_cast<uint32_t>(need);
            hdr->queuedAt = micros();
            hdr->invoke = fn;
            memcpy(queueBuf + queueTail + headerSize, closure, len);

            queueTail = (queueTail + need) % queueBytes;
            queueUsed += pad + need;
            queueJobs++;

            mutex_exit(&queueMu);

            publishDepth();
            return true;
        }
    }

    void pump() {
        drainOne();
    }

    void drain() {
        size_t guard = queueBytes / headerSize;
        while (guard-- > 0 && drainOne()) {
        }
    }

    uint32_t drainTask(void *param) {
        (void) param;

        // Draining shares core 0 with the web server, so it yields regularly
        // rather than emptying the queue in one go.
        constexpr size_t batch = 4;

        size_t done = 0;
        while (done < batch && drainOne()) {
            done++;
        }

        // Come back immediately while there is a backlog, otherwise poll slowly.
        return done == batch ? 1 : 20;
    }
}
