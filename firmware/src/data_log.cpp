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
    if (_file) return true;

    if (!sd.exists(DATA_LOG_PATH)) {
        String msgDir = DATA_LOG_PATH;
        msgDir.remove(msgDir.indexOf('/', 1));
        sd.mkdir(msgDir.c_str());
    }
    _file = sd.open(DATA_LOG_PATH, O_RDWR | O_CREAT);
    if (!_file) {
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
        _file.read(&_first, sizeof(LogRecordKey));
        _file.seek(_wrapPos - _recordSize);
        _file.read(&_last, sizeof(LogRecordKey));
    }

    if (_fileSize && _last.rev - _first.rev + 1 != _entries) {
        _file.close();
        sd.remove(DATA_LOG_PATH);

        LOGE("log: File %s damaged.\r\n", DATA_LOG_PATH);
        LOGE("log: Deleting %s and restarting.\r\n", DATA_LOG_PATH);

        rp2040.reboot();
    }

    return true;
}

void DataLog::end() {
    if (_file) {
        _file.close();
    }
}

uint32_t DataLog::entries() {
    return _entries;
}

uint32_t DataLog::firstRev() {
    return _first.rev;
}

uint32_t DataLog::firstTS() {
    return _first.ts;
}

uint32_t DataLog::lastRev() {
    return _last.rev;
}

uint32_t DataLog::lastTS() {
    return _last.ts;
}

uint32_t DataLog::fileSize() {
    return _fileSize;
}

error DataLog::read(uint32_t ts, LogRecord *rec) {
    ts -= ts % _interval;

    if (!_file) {
        return newError("file not open");
    }

    if (_entries == 0) {
        return newError("no entries");
    }
    if (ts < _first.ts) {
        // Before the beginning of the file.
        readRev(_first.rev, rec);
        rec->ts = ts;

        return {};
    }
    if (ts > _last.ts) {
        // Past the end of the file.
        readRev(_last.rev, rec);
        rec->ts = ts;

        return {};
    }

    // Check the last records cache if we are in the time range.
    if (ts >= _last.ts - _lastCacheSize * _interval) {
        for (int i = 0; i < _lastCacheSize; i++) {
            uint32_t cacheTS = _lastCache[i].ts;
            if (cacheTS == ts) {
                rp2040.memcpyDMA(rec, &_lastCache[i], sizeof(LogRecord));

                metrics.datalog_cache_hit.fetch_add(1, std::memory_order_relaxed);

                return {};
            }
        }
    }

    // Check the last read record cache, it is likely that reads will be sequential, so this will give us a good hint
    // of where to search.
    if (_lastReadTS < ts) {
        uint32_t rev = (ts - _lastReadTS) / _interval + _lastReadRev;
        readRev(rev, rec);
        if (rec->ts == ts) {
            return {};
        }
    }

    uint32_t lowRev = _first.rev;
    uint32_t lowTS = _first.ts;
    uint32_t highRev = _last.rev;
    uint32_t highTS = _last.ts;

    search(ts, rec, lowTS, lowRev, highTS, highRev);
    rec->ts = ts;

    return {};
}

error DataLog::write(LogRecord *rec) {
    if (!_file) {
        return newError("file not open");
    }
    if (rec->ts <= _last.ts) {
        return newError("timestamp not increasing");
    }
    rec->rev = ++_last.rev;
    _last.ts = rec->ts;

    // Add it to the last records cache.
    _lastCache[_lastCachePos++] = *rec;
    _lastCachePos %= _lastCacheSize;

    if (_wrapPos || _fileSize >= _maxFileSize) {
        // The file has/should wrap.
        _file.seek(_wrapPos);
        _wrapPos = (_wrapPos + _recordSize) % _fileSize;
        _file.write(rec, _recordSize);
        _file.flush();
        _file.seek(_wrapPos);

        // Read the new first key.
        auto key = LogRecordKey{};
        _file.read(&key, sizeof(LogRecordKey));
        _first = key;

        metrics.datalog_write_io.fetch_add(1, std::memory_order_relaxed);

        return {};
    }

    // No wrap, just write at the end of the file.
    _file.seek(_fileSize);
    _file.write(rec, _recordSize);
    _file.flush();

    _fileSize += _recordSize;
    _entries++;

    // If this is the first record, set the first timestamp and rev.
    if (_entries == 1) {
        _first.ts = rec->ts;
        _first.rev = rec->rev;
    }
    metrics.datalog_write_io.fetch_add(1, std::memory_order_relaxed);

    return {};
}

DataLog::LogRecordKey DataLog::readKey(uint32_t pos) {
    auto key = LogRecordKey{};
    _file.seek(pos);
    _file.read(&key, sizeof(LogRecordKey));

    metrics.datalog_read_io.fetch_add(1, std::memory_order_relaxed);

    return key;
}

uint8_t DataLog::readRev(uint32_t rev, LogRecord *rec) {
    if (rev < _first.rev || rev > _last.rev) {
        return 1;
    }

    uint32_t pos = ((rev - _first.rev) * _recordSize + _wrapPos) % _fileSize;

    _file.seek(pos);
    _file.read(rec, _recordSize);

    _lastReadTS = rec->ts;
    _lastReadRev = rec->rev;

    metrics.datalog_read_io.fetch_add(1, std::memory_order_relaxed);

    return 0;
}

void DataLog::search(const uint32_t ts, LogRecord *        rec,
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

uint32_t DataLog::findWrapPos(const uint32_t lowPos, const uint32_t lowTS, const uint32_t highPos,
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
