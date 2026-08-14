//
// Created by Nicholas Wiersma on 2025/10/23.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "data_log.h"
#endif

bool DataLog::begin() {
    if (_open) return true;

    bool damaged = false;

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.mkdirFor(DATA_LOG_PATH); e) {
            return e;
        }

        sd.pin(DATA_LOG_PATH);

        _fileSize = sd.size(DATA_LOG_PATH);
        _maxFileSize = max(_fileSize, _maxFileSize);
        if (_fileSize) {
            _first = readKey(sd, 0);
            _last = readKey(sd, _fileSize - _recordSize);
            _entries = _fileSize / _recordSize;

            LOGD("Found %d entries in log file", _entries);
        }

        if (_first.ts > _last.ts) {
            _wrapPos = findWrapPos(sd, 0, _first.ts, _fileSize - _recordSize, _last.ts);
            _first = readKey(sd, _wrapPos);
            _last = readKey(sd, _wrapPos - _recordSize);
        }

        if (_fileSize && _last.rev - _first.rev + 1 != _entries) {
            damaged = true;
            return sd.remove(DATA_LOG_PATH);
        }

        _open = true;
        return {};
    });

    if (damaged) {
        LOGE("log: File %s damaged.\r\n", DATA_LOG_PATH);
        LOGE("log: Deleting %s and restarting.\r\n", DATA_LOG_PATH);
        rp2040.reboot();
        return false;
    }

    if (err) {
        LOGE("log: Could not open %s: %s", DATA_LOG_PATH, err.Error());
        return false;
    }
    return _open;
}

void DataLog::end() {
    // Anything still queued must land before the handle is closed.
    storage::drain();

    mutex_enter_blocking(&_mu);
    _open = false;
    mutex_exit(&_mu);

    storage::run([](storage::sdAccess &sd) {
        sd.closeAll();
        return error{};
    });
}

uint32_t DataLog::entries() {
    mutex_enter_blocking(&_mu);
    auto e = _entries;
    mutex_exit(&_mu);
    return e;
}

uint32_t DataLog::firstRev() {
    mutex_enter_blocking(&_mu);
    auto r = _first.rev;
    mutex_exit(&_mu);
    return r;
}

uint32_t DataLog::firstTS() {
    mutex_enter_blocking(&_mu);
    auto t = _first.ts;
    mutex_exit(&_mu);
    return t;
}

uint32_t DataLog::lastRev() {
    mutex_enter_blocking(&_mu);
    auto r = _last.rev;
    mutex_exit(&_mu);
    return r;
}

uint32_t DataLog::lastTS() {
    mutex_enter_blocking(&_mu);
    auto t = _last.ts;
    mutex_exit(&_mu);
    return t;
}

uint32_t DataLog::fileSize() {
    mutex_enter_blocking(&_mu);
    auto s = _fileSize;
    mutex_exit(&_mu);
    return s;
}

error DataLog::read(uint32_t ts, LogRecord *rec, uint32_t timeoutMS) {
    ts -= ts % _interval;

    if (timeoutMS > 0) {
        if (!mutex_enter_timeout_ms(&_mu, timeoutMS)) {
            return newError("mutex timeout");
        }
    } else {
        mutex_enter_blocking(&_mu);
    }

    if (!_open) {
        mutex_exit(&_mu);
        return newError("file not open");
    }

    if (_entries == 0) {
        mutex_exit(&_mu);
        return newError("no entries");
    }

    // Check the last records cache before entering a transaction, so a cache
    // hit is never delayed by a card stall.
    if (ts >= _last.ts - _lastCacheSize * _interval) {
        for (int i = 0; i < _lastCacheSize; i++) {
            uint32_t cacheTS = _lastCache[i].ts;
            if (cacheTS == ts) {
                rp2040.memcpyDMA(rec, &_lastCache[i], sizeof(LogRecord));

                metrics.datalog_cache_hit.fetch_add(1, std::memory_order_relaxed);

                mutex_exit(&_mu);
                return {};
            }
        }
    }

    mutex_exit(&_mu);

    // The whole search runs in one transaction rather than taking the card lock
    // per seek. The bounds are re-read inside it: a write may have landed while
    // we were waiting for the card, and readRev validates against them.
    return storage::run([&](storage::sdAccess &sd) -> error {
        const LogRecordKey first = _first;
        const LogRecordKey last = _last;

        if (ts < first.ts) {
            // Before the beginning of the file.
            if (readRev(sd, first.rev, rec)) {
                return newError("could not read record");
            }
            rec->ts = ts;
            return {};
        }
        if (ts > last.ts) {
            // Past the end of the file.
            if (readRev(sd, last.rev, rec)) {
                return newError("could not read record");
            }
            rec->ts = ts;
            return {};
        }

        // Reads are likely to be sequential, so the last read record gives a
        // good hint of where to search.
        if (_lastReadTS < ts) {
            uint32_t rev = (ts - _lastReadTS) / _interval + _lastReadRev;
            if (!readRev(sd, rev, rec) && rec->ts == ts) {
                return {};
            }
        }

        if (search(sd, ts, rec, first.ts, first.rev, last.ts, last.rev)) {
            return newError("could not read record");
        }
        rec->ts = ts;
        return {};
    }, timeoutMS > 0 ? timeoutMS : SD_LOCK_TIMEOUT_MS);
}

error DataLog::queueWrite(const LogRecord *rec) {
    auto job = [this, r = *rec](storage::sdAccess &sd) mutable -> error {
        const auto start = micros();

        auto err = this->write(sd, &r);
        if (err) {
            metrics.datalog_write_errors_total.fetch_add(1, std::memory_order_relaxed);
            LOGE("Error writing datalog: %s", err.Error());
            return err;
        }

        const uint32_t took = micros() - start;
        metrics.datalog_write_time_us_total.fetch_add(took, std::memory_order_relaxed);

        uint32_t slowest = metrics.datalog_write_time_us_max.load(std::memory_order_relaxed);
        while (took > slowest &&
               !metrics.datalog_write_time_us_max.compare_exchange_weak(
                   slowest, took, std::memory_order_relaxed)) {
        }
        return {};
    };

#ifdef UNIT_TEST
    // Run inline so tests observe the write's own error rather than only
    // whether it was accepted onto the queue.
    return storage::run(job);
#else
    if (!storage::submit(job)) {
        return newError("sd queue full");
    }
    return {};
#endif
}

error DataLog::write(storage::sdAccess &sd, LogRecord *rec) {
    mutex_enter_blocking(&_mu);

    if (!_open) {
        mutex_exit(&_mu);
        return newError("file not open");
    }
    if (rec->ts <= _last.ts) {
        mutex_exit(&_mu);
        return newError("timestamp not increasing");
    }

    // The file has/should wrap.
    const bool     wrapping = _wrapPos || _fileSize >= _maxFileSize;
    const uint32_t writePos = wrapping ? _wrapPos : _fileSize;

    // Snapshot for rollback should the card write fail.
    const LogRecordKey prevFirst = _first;
    const LogRecordKey prevLast = _last;
    const uint32_t     prevWrapPos = _wrapPos;
    const uint32_t     prevFileSize = _fileSize;
    const uint32_t     prevEntries = _entries;
    const uint32_t     prevCachePos = _lastCachePos;

    rec->rev = ++_last.rev;
    _last.ts = rec->ts;

    // Cached before the card I/O so readers are served from RAM while the card
    // is busy.
    const uint32_t cachePos = _lastCachePos;
    _lastCache[_lastCachePos++] = *rec;
    _lastCachePos %= _lastCacheSize;

    if (wrapping) {
        _wrapPos = (_wrapPos + _recordSize) % _fileSize;
    } else {
        _fileSize += _recordSize;
        _entries++;

        // If this is the first record, set the first timestamp and rev.
        if (_entries == 1) {
            _first.ts = rec->ts;
            _first.rev = rec->rev;
        }
    }
    const uint32_t nextWrapPos = _wrapPos;
    mutex_exit(&_mu);

    error        err = sd.writeAt(DATA_LOG_PATH, writePos, rec, _recordSize);
    LogRecordKey newFirst{};
    if (!err && wrapping) {
        // Reads the new first key. Failing here must roll back rather than
        // publish a zeroed _first, which would corrupt the (rev - _first.rev)
        // arithmetic in readRev for every later seek.
        err = sd.readAt(DATA_LOG_PATH, nextWrapPos, &newFirst, sizeof(LogRecordKey));
    }

    mutex_enter_blocking(&_mu);
    if (err) {
        _first = prevFirst;
        _last = prevLast;
        _wrapPos = prevWrapPos;
        _fileSize = prevFileSize;
        _entries = prevEntries;
        _lastCache[cachePos] = LogRecord{};
        _lastCachePos = prevCachePos;
        mutex_exit(&_mu);
        return err;
    }
    if (wrapping) {
        _first = newFirst;
    }
    mutex_exit(&_mu);

    metrics.datalog_write_io.fetch_add(1, std::memory_order_relaxed);
    return {};
}

DataLog::LogRecordKey DataLog::readKey(storage::sdAccess &sd, const uint32_t pos) {
    auto key = LogRecordKey{};
    sd.readAt(DATA_LOG_PATH, pos, &key, sizeof(LogRecordKey));
    return key;
}

uint8_t DataLog::readRev(storage::sdAccess &sd, const uint32_t rev, LogRecord *rec) {
    if (rev < _first.rev || rev > _last.rev) {
        return 1;
    }

    uint32_t pos = ((rev - _first.rev) * _recordSize + _wrapPos) % _fileSize;

    if (auto err = sd.readAt(DATA_LOG_PATH, pos, rec, _recordSize); err) {
        return 1;
    }

    _lastReadTS = rec->ts;
    _lastReadRev = rec->rev;
    return 0;
}

uint8_t DataLog::search(storage::sdAccess &sd, const uint32_t ts, LogRecord *rec,
                        const uint32_t     lowTS, const uint32_t lowRev,
                        const uint32_t     highTS, const uint32_t highRev) {
    if (highRev - lowRev <= 1) {
        return readRev(sd, lowRev, rec);
    }
    if (readRev(sd, (lowRev + highRev) / 2, rec)) {
        return 1;
    }
    if (rec->ts == ts) {
        return 0;
    }

    // A record outside the bounds means the file disagrees with the metadata.
    // Recursing on it would not converge.
    if (rec->rev <= lowRev || rec->rev >= highRev) {
        return 1;
    }

    if (rec->ts < ts) {
        return search(sd, ts, rec, rec->ts, rec->rev, highTS, highRev);
    }
    return search(sd, ts, rec, lowTS, lowRev, rec->ts, rec->rev);
}

uint32_t DataLog::findWrapPos(storage::sdAccess &sd, const uint32_t lowPos, const uint32_t lowTS,
                              const uint32_t     highPos, const uint32_t highTS) {
    if (highPos - lowPos == _recordSize) {
        return highPos;
    }
    uint32_t midPos = (lowPos + highPos) / 2;
    midPos += midPos % _recordSize;

    uint32_t midTS = readKey(sd, midPos).ts;
    if (midTS > lowTS) {
        return findWrapPos(sd, midPos, midTS, highPos, highTS);
    }
    return findWrapPos(sd, lowPos, lowTS, midPos, midTS);
}
