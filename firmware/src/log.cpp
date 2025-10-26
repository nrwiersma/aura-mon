//
// Created by Nicholas Wiersma on 2025/10/23.
//

#include "auramon.h"

int8_t log::begin() {
    if (_file) return 0;

    mutex_enter_blocking(&sdMu);
    if (!sd.exists(DATA_LOG_PATH)) {
        String msgDir = DATA_LOG_PATH;
        msgDir.remove(msgDir.indexOf('/', 1));
        if (!sd.exists(msgDir.c_str())) {
            if (!sd.mkdir(msgDir.c_str())) {
                return 1;
            }
        }
    }
    _file = sd.open(DATA_LOG_PATH);
    if (!_file) {
        return 1;
    }

    _fileSize = _file.size();
    _maxFileSize = max(_fileSize, _maxFileSize);
    if (_fileSize) {
        _first = readKey(0);
        _last = readKey(_fileSize - _recordSize);
        _entries = _fileSize / _recordSize;
    }

    if (_first.ts > _last.ts) {
        // The file has wrapped around. Find the wrap point and
        // recalculate the first and last keys.
        _wrapPos = findWrapPos(0, _first.ts, _fileSize - _recordSize, _last.ts);
        _first = readKey(_wrapPos);
        _last = readKey(_wrapPos - _recordSize);
    }

    if (_last.rev - _first.rev + 1 != _entries) {
        LOGE("log: File %s damaged.\r\n", DATA_LOG_PATH);
        LOGE("log: Deleting %s and restarting.\r\n", DATA_LOG_PATH);
        _file.close();
        sd.remove(DATA_LOG_PATH);
        rp2040.reboot();
    }
    mutex_exit(&sdMu);

    return 0;
}

int8_t log::read(uint32_t ts, logRecord *rec, uint32_t timeoutMS) {
    ts -= ts % _interval;

    if (timeoutMS > 0) {
        if (!mutex_enter_timeout_ms(&sdMu, timeoutMS)) {
            return -1;
        }
    } else {
        mutex_enter_blocking(&_mu);
    }

    if (!_file) {
        mutex_exit(&_mu);
        return 1;
    }
    if (_entries == 0) {
        mutex_exit(&_mu);
        return 2;
    }
    if (ts < _first.ts) {
        // Before the beginning of the file.
        readRev(_first.rev, rec);
        rec->ts = ts;

        mutex_exit(&_mu);
        return 1;
    }
    if (ts >= _last.ts) {
        // Past the end of the file.
        readRev(_last.rev, rec);
        rec->ts = ts;
        if (ts == _last.ts) {
            mutex_exit(&_mu);
            return 0;
        }
        mutex_exit(&_mu);
        return 1;
    }

    // Check the last records cache if we are in the time range.
    if (ts >= _last.ts-_lastCacheSize*_interval) {
        for (int i = 0; i < _lastCacheSize; i++) {
            uint32_t cacheTS = _lastCache[i].ts;
            if (cacheTS == ts) {
                memcpy(rec, &_lastCache[i], sizeof(logRecord));
                return 0;
            }
        }
    }

    uint32_t lowRev = _first.rev;
    uint32_t lowTS = _first.ts;
    uint32_t highRev = _last.ts;
    uint32_t highTS = _last.rev;

    // Limit the search space by checking the read cache,
    // it will give hits in the correct direction to search.
    for (int i = 0; i < _readCacheSize; i++) {
        uint32_t cacheTS = _readCache[i].ts;
        if (cacheTS == ts) {
            // If the cache position is not changed to the current
            // item, you could potentially fill the read cache with
            // a single record key.
            _readCachePos = i;
            readRev(_readCache[i].rev, rec);

            mutex_exit(&_mu);
            return 0;
        }
        if (cacheTS > lowRev && cacheTS < ts) {
            lowTS = cacheTS;
            lowRev = _readCache[i].rev;
        } else if (cacheTS < highRev && cacheTS > ts) {
            highTS = cacheTS;
            highRev = _readCache[i].rev;
        }
    }
    search(ts, rec, lowTS, lowRev, highTS, highRev);
    rec->ts = ts;

    mutex_exit(&_mu);
    return 0;
}

int8_t log::write(logRecord *rec) {
    mutex_enter_blocking(&_mu);

    if (!_file) {
        mutex_exit(&_mu);
        return 1;
    }
    if (rec->ts <= _last.ts) {
        mutex_exit(&_mu);
        return 2;
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

        _fileIO++;

        mutex_exit(&_mu);
        return 0;
    }

    // No wrap, just write at the end of the file.
    mutex_enter_blocking(&sdMu);
    _file.seek(_fileSize);
    _file.write(rec, _recordSize);
    _file.flush();
    mutex_exit(&sdMu);
    _fileSize += _recordSize;
    _entries++;

    // If this is the first record, set the first timestamp.
    if (_first.ts == 0) {
        _first.ts = rec->ts;
    }
    _fileIO++;

    mutex_exit(&_mu);
    return 0;
}

log::logRecordKey log::readKey(uint32_t pos) {
    mutex_enter_blocking(&sdMu);
    auto key = logRecordKey{};
    _file.seek(pos);
    _file.read(&key, sizeof(logRecordKey));
    mutex_exit(&sdMu);
    return key;
}

uint8_t log::readRev(uint32_t rev, logRecord *rec) {
    if (rev < _first.rev || rev > _last.rev) {
        return 1;
    }

    mutex_enter_blocking(&sdMu);
    uint32_t pos = ((rev - _first.rev) * _recordSize + _wrapPos) % _fileSize;
    _file.seek(pos);
    _file.read(rec, _recordSize);
    mutex_exit(&sdMu);

    _readCache[_readCachePos++] = logRecordKey{rec->rev, rec->ts};
    _readCachePos %= _readCacheSize;
    _fileIO++;
    return 0;
}

void log::search(const uint32_t ts, logRecord *        rec,
                 const uint32_t lowTS, const uint32_t  lowRev,
                 const uint32_t highTS, const uint32_t highRev) {
    // This is straight out of IoTaWatt and very smart. Check if this section of the
    // file is gapless, and if not, potentially drastically limit the search space by
    // getting the limit from the other limits' perspective.
    uint32_t floorRev = max(lowRev, highRev - (uint32_t) ((highTS - ts) / _interval));
    uint32_t ceilRev = min(highRev, lowRev + (uint32_t) ((ts - lowTS) / _interval));
    if (ceilRev < highRev || floorRev == ceilRev) {
        readRev(ceilRev, rec);
        if (rec->ts == ts) {
            return;
        }
        search(ts, rec, lowTS, lowRev, rec->ts, rec->rev);
        return;
    }
    if (floorRev > lowRev) {
        readRev(floorRev, rec);
        if (rec->ts == ts) {
            return;
        }
        search(ts, rec, rec->ts, rec->rev, highTS, highRev);
        return;
    }

    // That did not narrow things, follow a normal binary search.
    if ((highRev - lowRev) <= 1) {
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

uint32_t log::findWrapPos(const uint32_t lowPos, const uint32_t lowTS, const uint32_t highPos, const uint32_t highTS) {
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
