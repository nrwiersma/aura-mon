//
// Created by Nicholas Wiersma on 2025/10/23.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestAuraMon.h"
#include "datalog.h"
#endif

bool dataLog::begin() {
    if (_file) return true;

    mutex_enter_blocking(&sdMu);
    if (!sd.exists(DATA_LOG_PATH)) {
        String msgDir = DATA_LOG_PATH;
        msgDir.remove(msgDir.indexOf('/', 1));
        sd.mkdir(msgDir.c_str());
    }
    _file = sd.open(DATA_LOG_PATH, O_RDWR | O_CREAT);
    if (!_file) {
        mutex_exit(&sdMu);
        return false;
    }

    _fileSize = _file.size();
    _maxFileSize = max(_fileSize, _maxFileSize);
    if (_fileSize) {
        _first = readKey(0);
        _last = readKey(_fileSize - _recordSize);
        _entries = _fileSize / _recordSize;

        LOGD("Found %d entries in log file", _entries);
    }

    if (_first.ts > _last.ts) {
        _wrapPos = findWrapPos(0, _first.ts, _fileSize - _recordSize, _last.ts);
        _file.seek(_wrapPos);
        _file.read(&_first, sizeof(logRecordKey));
        _file.seek(_wrapPos - _recordSize);
        _file.read(&_last, sizeof(logRecordKey));
    }

    if (_fileSize && _last.rev - _first.rev + 1 != _entries) {
        LOGE("log: File %s damaged.\r\n", DATA_LOG_PATH);
        LOGE("log: Deleting %s and restarting.\r\n", DATA_LOG_PATH);
        _file.close();
        sd.remove(DATA_LOG_PATH);
        rp2040.reboot();
    }

    mutex_exit(&sdMu);
    return true;
}

uint32_t dataLog::entries() {
    mutex_enter_blocking(&_mu);
    auto e = _entries;
    mutex_exit(&_mu);
    return e;
}

uint32_t dataLog::firstRev() {
    mutex_enter_blocking(&_mu);
    auto r = _first.rev;
    mutex_exit(&_mu);
    return r;
}

uint32_t dataLog::firstTS() {
    mutex_enter_blocking(&_mu);
    auto t = _first.ts;
    mutex_exit(&_mu);
    return t;
}

uint32_t dataLog::lastRev() {
    mutex_enter_blocking(&_mu);
    auto r = _last.rev;
    mutex_exit(&_mu);
    return r;
}

uint32_t dataLog::lastTS() {
    mutex_enter_blocking(&_mu);
    auto t = _last.ts;
    mutex_exit(&_mu);
    return t;
}

uint32_t dataLog::fileSize() {
    mutex_enter_blocking(&_mu);
    auto s = _fileSize;
    mutex_exit(&_mu);
    return s;
}

error *dataLog::read(uint32_t ts, logRecord *rec, uint32_t timeoutMS) {
    ts -= ts % _interval;

    if (timeoutMS > 0) {
        if (!mutex_enter_timeout_ms(&_mu, timeoutMS)) {
            return newError("mutex timeout");
        }
    } else {
        mutex_enter_blocking(&_mu);
    }

    if (!_file) {
        mutex_exit(&_mu);
        return newError("file not open");
    }

    if (_entries == 0) {
        mutex_exit(&_mu);
        return newError("no entries");
    }
    if (ts < _first.ts) {
        // Before the beginning of the file.

        readRev(_first.rev, rec);
        rec->ts = ts;

        mutex_exit(&_mu);
        return nullptr;
    }
    if (ts >= _last.ts) {
        // Past the end of the file.
        readRev(_last.rev, rec);
        rec->ts = ts;
        if (ts == _last.ts) {
            mutex_exit(&_mu);
            return nullptr;
        }

        mutex_exit(&_mu);
        return nullptr;
    }

    // Check the last records cache if we are in the time range.
    if (ts >= _last.ts - _lastCacheSize * _interval) {
        for (int i = 0; i < _lastCacheSize; i++) {
            uint32_t cacheTS = _lastCache[i].ts;
            if (cacheTS == ts) {
                rp2040.memcpyDMA(rec, &_lastCache[i], sizeof(logRecord));

                metrics.datalog_cache_hit.fetch_add(1, std::memory_order_relaxed);

                mutex_exit(&_mu);
                return nullptr;
            }
        }
    }

    // Check the last read record cache, it is likely that reads will be sequential, so this will give us a good hint
    // of where to search.
    if (_lastReadTS < ts) {
        uint32_t rev = (ts - _lastReadTS) / _interval + _lastReadRev;
        readRev(rev, rec);
        if (rec->ts == ts) {
            mutex_exit(&_mu);
            return nullptr;
        }
    }

    uint32_t lowRev = _first.rev;
    uint32_t lowTS = _first.ts;
    uint32_t highRev = _last.rev;
    uint32_t highTS = _last.ts;

    search(ts, rec, lowTS, lowRev, highTS, highRev);
    rec->ts = ts;

    mutex_exit(&_mu);
    return nullptr;
}

error *dataLog::write(logRecord *rec) {
    mutex_enter_blocking(&_mu);

    if (!_file) {
        mutex_exit(&_mu);
        return newError("file not open");
    }
    if (rec->ts <= _last.ts) {
        mutex_exit(&_mu);
        return newError("timestamp not increasing");
    }
    rec->rev = ++_last.rev;
    _last.ts = rec->ts;

    // Add it to the last records cache.
    _lastCache[_lastCachePos++] = *rec;
    _lastCachePos %= _lastCacheSize;

    if (_wrapPos || _fileSize >= _maxFileSize) {
        // The file has/should wrap.
        mutex_enter_blocking(&sdMu);
        _file.seek(_wrapPos);
        _wrapPos = (_wrapPos + _recordSize) % _fileSize;
        _file.write(rec, _recordSize);
        _file.flush();

        // Read the new first key.
        auto key = logRecordKey{};
        _file.read(&key, sizeof(logRecordKey));
        _first = key;
        mutex_exit(&sdMu);

        metrics.datalog_io.fetch_add(1, std::memory_order_relaxed);

        mutex_exit(&_mu);
        return nullptr;
    }

    // No wrap, just write at the end of the file.
    mutex_enter_blocking(&sdMu);
    _file.seek(_fileSize);
    _file.write(rec, _recordSize);
    _file.flush();
    mutex_exit(&sdMu);

    _fileSize += _recordSize;
    _entries++;

    // If this is the first record, set the first timestamp and rev.
    if (_entries == 1) {
        _first.ts = rec->ts;
        _first.rev = rec->rev;
    }
    metrics.datalog_io.fetch_add(1, std::memory_order_relaxed);

    mutex_exit(&_mu);
    return nullptr;
}

dataLog::logRecordKey dataLog::readKey(uint32_t pos) {
    auto key = logRecordKey{};
    _file.seek(pos);
    _file.read(&key, sizeof(logRecordKey));
    return key;
}

uint8_t dataLog::readRev(uint32_t rev, logRecord *rec) {
    if (rev < _first.rev || rev > _last.rev) {
        return 1;
    }

    uint32_t pos = ((rev - _first.rev) * _recordSize + _wrapPos) % _fileSize;

    mutex_enter_blocking(&sdMu);
    _file.seek(pos);
    _file.read(rec, _recordSize);
    mutex_exit(&sdMu);

    _lastReadTS = rec->ts;
    _lastReadRev = rec->rev;

    metrics.datalog_io.fetch_add(1, std::memory_order_relaxed);

    return 0;
}

void dataLog::search(const uint32_t ts, logRecord *        rec,
                     const uint32_t lowTS, const uint32_t  lowRev,
                     const uint32_t highTS, const uint32_t highRev) {
    if (highRev - lowRev <= 1) {
        readRev(lowRev, rec);
        return;
    }
    readRev((lowRev + highRev) / 2, rec);
    if (rec->ts == ts) {
        return;
    }
    if (rec->ts < ts) {
        search(ts, rec, rec->ts, rec->rev, highTS, highRev);
        return;
    }
    search(ts, rec, lowTS, lowRev, rec->ts, rec->rev);
}

uint32_t dataLog::findWrapPos(const uint32_t lowPos, const uint32_t lowTS, const uint32_t highPos,
                              const uint32_t highTS) {
    if (highPos - lowPos == _recordSize) {
        return highPos;
    }
    uint32_t midPos = (lowPos + highPos) / 2;
    midPos += midPos % _recordSize;

    uint32_t midTS = readKey(midPos).ts;
    if (midTS > lowTS) {
        return findWrapPos(midPos, midTS, highPos, highTS);
    }
    return findWrapPos(lowPos, lowTS, midPos, midTS);
}
