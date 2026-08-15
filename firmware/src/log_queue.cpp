//
// Created by Nicholas Wiersma on 2026/08/14.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif
#include "log_queue.h"

// Records are copied in and out of the queue by value, so a record must never
// hold anything that cannot simply be copied, such as a pointer to memory it
// owns.
static_assert(std::is_trivially_copyable_v<LogRecord>, "LogRecord must be copyable by value");

bool logQueue::push(const LogRecord *rec) {
    if (!mutex_enter_timeout_ms(&_mu, LOCK_TIMEOUT_MS)) {
        return false;
    }

    if (_count == LOG_QUEUE_SIZE) {
        mutex_exit(&_mu);
        return false;
    }

    // Take a copy so the caller is free to reuse their record.
    _recs[_tail] = *rec;
    _tail = (_tail + 1) % LOG_QUEUE_SIZE;
    _count++;

    mutex_exit(&_mu);
    return true;
}

bool logQueue::pop(LogRecord *rec) {
    if (!mutex_enter_timeout_ms(&_mu, LOCK_TIMEOUT_MS)) {
        return false;
    }

    if (_count == 0) {
        mutex_exit(&_mu);
        return false;
    }

    // Copy the record out, the queued slot is free to be reused.
    *rec = _recs[_head];
    _head = (_head + 1) % LOG_QUEUE_SIZE;
    _count--;

    mutex_exit(&_mu);
    return true;
}

uint32_t logQueue::size() {
    mutex_enter_blocking(&_mu);
    auto c = _count;
    mutex_exit(&_mu);
    return c;
}
