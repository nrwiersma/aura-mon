//
// Created by Nicholas Wiersma on 2026/08/14.
//

#pragma once

#include <type_traits>

#include "data_log.h"

// The number of records that can be queued before writes back up.
#define LOG_QUEUE_SIZE 8

// logQueue is a fixed size FIFO queue of log records, used to hand records
// from the core producing them to the core writing them to the data log.
//
// Records are copied in and out of the queue, so the caller never shares
// memory with the queue.
class logQueue {
public:
    logQueue() { mutex_init(&_mu); }

    // push copies a record onto the back of the queue, returning false if the
    // queue is full or the lock could not be taken.
    bool push(const LogRecord *rec);

    // pop copies the record at the front of the queue into rec, returning
    // false if the queue is empty or the lock could not be taken.
    bool pop(LogRecord *rec);

    // size returns the number of records waiting in the queue.
    uint32_t size();

private:
    // The lock is only ever held for a single record copy.
    static constexpr uint32_t LOCK_TIMEOUT_MS = 10;

    mutex_t _mu{};

    LogRecord _recs[LOG_QUEUE_SIZE]{};
    uint32_t  _head = 0;
    uint32_t  _tail = 0;
    uint32_t  _count = 0;
};
