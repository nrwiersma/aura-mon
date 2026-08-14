//
// Created by Nicholas Wiersma on 2026/08/13.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <ArduinoJson.h>
#include <errors.h>

// SdFat allows the card up to SD_WRITE_TIMEOUT (600ms) of busy time during a
// write, so any lock guarding card access must wait at least that long or it
// will spuriously fail during a normal card garbage collection stall.
#define SD_LOCK_TIMEOUT_MS 750

namespace storage {
    // Bytes reserved for queued transactions. Entries are variable length; the
    // largest is a datalog write closure carrying a 384 byte LogRecord.
    constexpr size_t queueBytes = 4096;

    // Entries are 8 byte aligned so captured doubles stay naturally aligned
    // once the closure is copied into the ring.
    constexpr size_t queueAlign = 8;

    // Largest single entry, header included.
    constexpr size_t jobMax = 512;

    class sdAccess;

    namespace detail {
        using invokeFn = error (*)(void *closure, sdAccess &sd);

        struct accessFactory;

        bool lockEnter(uint32_t timeoutMS);
        void lockExit();
        bool pushJob(invokeFn fn, const void *closure, size_t len);
    }

    // Proof that the card lock is held. Every card primitive hangs off this
    // type, and it can only be constructed by run() or by the drain task, so
    // the card cannot be reached without holding the lock.
    class sdAccess {
    public:
        sdAccess(const sdAccess &) = delete;
        sdAccess &operator=(const sdAccess &) = delete;

        // Reads len bytes from pos. A short read is an error.
        error readAt(const char *path, uint32_t pos, void *buf, size_t len);

        // Writes len bytes at pos and flushes, creating the file if needed.
        error writeAt(const char *path, uint32_t pos, const void *buf, size_t len);

        // Writes len bytes to the end of the file and flushes, creating the
        // file, and any missing parent directory, if needed.
        error append(const char *path, const void *buf, size_t len);

        // Reads path and parses it into doc.
        error readJSON(const char *path, JsonDocument &doc);

        // Serialises doc to tmpPath, then replaces path with it, so a failure
        // part way through leaves the previous contents of path intact.
        error writeJSON(const char *path, const char *tmpPath, const JsonDocument &doc);

        // Returns the size of path in bytes, or zero if it does not exist.
        uint32_t size(const char *path);

        bool  exists(const char *path);
        bool  isDirectory(const char *path);
        error remove(const char *path);
        error rename(const char *from, const char *to);

        // Creates the parent directory of path if it does not already exist.
        error mkdirFor(const char *path);

        // Keeps the handle for path open between transactions, so a sequence of
        // seeks or a chunked transfer does not reopen the file every time. The
        // handle is still dropped if the file is removed or renamed through
        // these primitives.
        void pin(const char *path);
        void unpin(const char *path);

        // Flushes and closes every open handle, pinned ones included.
        void closeAll();

    private:
        sdAccess() = default;

        friend struct detail::accessFactory;
    };

    namespace detail {
        struct accessFactory {
            static sdAccess make() { return sdAccess(); }
        };
    }

    // Prepares the queue and releases any open handle. Called once at boot.
    void init();

    // Brings the card up, reporting false if it could not be initialised. The
    // reason is printed to Serial, as this runs before logging can persist.
    bool begin();

    // Terminates the SDIO peripheral and blocks all further card access. Used
    // on the reboot path.
    void end();

    // Reports whether the card is responding, and its last error code.
    bool status(uint8_t &errorCode);

    // Reports whether the calling core is inside a transaction.
    bool inTransaction();

    // Runs fn on the calling core with exclusive card access, returning fn's
    // error or an error if the card could not be claimed in time.
    template<typename F>
    error run(F &&fn, const uint32_t timeoutMS = SD_LOCK_TIMEOUT_MS) {
        if (!detail::lockEnter(timeoutMS)) {
            return newError("sd card busy");
        }

        sdAccess sd = detail::accessFactory::make();
        error    err = fn(sd);

        detail::lockExit();
        return err;
    }

    // Queues fn to run on the drain task with exclusive card access, returning
    // false if the queue is full. The caller decides whether to retry or drop.
    //
    // This may be called from inside a transaction.
    template<typename F>
    bool submit(F &&fn) {
        using job = std::decay_t<F>;

        // The closure is copied byte-wise into the queue, so it must not own
        // anything. Capturing a String, or any type with a destructor, is a bug.
        static_assert(std::is_trivially_copyable_v<job>,
                      "queued closures are copied byte-wise; capture only trivially copyable types");
        static_assert(alignof(job) <= queueAlign,
                      "closure is over-aligned for the queue");

        job local = std::forward<F>(fn);

#ifdef UNIT_TEST
        // Native tests have no task loop, so run through to keep assertions
        // synchronous while still exercising the transaction body.
        run([&local](sdAccess &sd) { return local(sd); });
        return true;
#else
        const detail::invokeFn thunk = [](void *c, sdAccess &sd) -> error {
            return (*static_cast<job *>(c))(sd);
        };
        return detail::pushJob(thunk, &local, sizeof(job));
#endif
    }

    // Runs at most one queued job, doing nothing inside a transaction. Long
    // running handlers call this so queued work still lands mid-response.
    void pump();

    // Runs queued jobs until the queue is empty. Bounded, so a failing card or
    // a job that queues more work cannot spin forever.
    void drain();

    // Runs queued jobs a few at a time. Must be registered on core 0 only, as
    // core 1 runs a watchdog a card stall can outlast.
    uint32_t drainTask(void *param);
}
