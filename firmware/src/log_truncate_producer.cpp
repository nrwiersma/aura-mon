//
// Created by Nicholas Wiersma on 2026/01/23.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "log_truncate_producer.h"
#endif

#include <cstring>

#include "log_truncate_producer.h"

namespace {
constexpr char kTempPath[] = MESSAGE_LOG_PATH ".trunc";
}

HttpResponseProducer::Status LogTruncateProducer::fail(int statusCode, const char *reason) {
    _statusCode = statusCode;
    _errorReason = reason;
    _state = State::Error;
    return Status::Error;
}

HttpResponseProducer::Status LogTruncateProducer::produce(uint8_t * /*buf*/, size_t /*cap*/, size_t &written) {
    written = 0;

    switch (_state) {
        case State::Start: {
            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }
            if (!sd.exists(MESSAGE_LOG_PATH)) {
                mutex_exit(&sdMu);
                return fail(404, "not found");
            }
            _src = sd.open(MESSAGE_LOG_PATH, O_READ);
            mutex_exit(&sdMu);
            if (!_src) {
                return fail(500, "could not open log");
            }
            _chunkEnd = _src.size();
            _state = State::ScanBackward;
            return Status::Pending;
        }

        case State::ScanBackward: {
            if (_chunkEnd == 0 || _markerFound) {
                _startOffset = _markerFound ? _lastMarkerOffset : 0;
                _state = State::OpenTemp;
                return Status::Pending;
            }

            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }

            const uint32_t chunkStart = (_chunkEnd > kChunkSize) ? (_chunkEnd - kChunkSize) : 0;
            const size_t chunkLen = _chunkEnd - chunkStart;

            if (!_src.seek(chunkStart)) {
                mutex_exit(&sdMu);
                _src.close();
                return fail(500, "could not seek log");
            }
            const int readLen = _src.read(_window, chunkLen);
            mutex_exit(&sdMu);
            if (readLen < 0 || static_cast<size_t>(readLen) != chunkLen) {
                _src.close();
                return fail(500, "could not read log");
            }

            if (_overlapLen > 0) {
                memcpy(_window + chunkLen, _overlap, _overlapLen);
            }

            const size_t totalLen = chunkLen + _overlapLen;
            if (totalLen >= kRestartMarkerLen) {
                for (size_t i = totalLen - kRestartMarkerLen + 1; i > 0; i--) {
                    const size_t idx = i - 1;
                    if (memcmp(_window + idx, kRestartMarker, kRestartMarkerLen) == 0) {
                        _lastMarkerOffset = chunkStart + idx;
                        _markerFound = true;
                        break;
                    }
                }
            }

            _overlapLen = min(chunkLen, kRestartMarkerLen - 1);
            if (_overlapLen > 0) {
                memcpy(_overlap, _window, _overlapLen);
            }

            _chunkEnd = chunkStart;
            return Status::Pending;
        }

        case State::OpenTemp: {
            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }
            if (!_src.seek(_startOffset)) {
                mutex_exit(&sdMu);
                _src.close();
                return fail(500, "could not seek log");
            }
            _tmp = sd.open(kTempPath, O_WRITE | O_CREAT | O_TRUNC);
            mutex_exit(&sdMu);
            if (!_tmp) {
                _src.close();
                return fail(500, "could not open temp log");
            }
            _state = State::CopyForward;
            return Status::Pending;
        }

        case State::CopyForward: {
            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }
            const int readLen = _src.read(_copyBuf, sizeof(_copyBuf));
            if (readLen <= 0) {
                mutex_exit(&sdMu);
                _state = State::Finalize;
                return Status::Pending;
            }
            const size_t wroteLen = _tmp.write(_copyBuf, static_cast<size_t>(readLen));
            mutex_exit(&sdMu);
            if (wroteLen != static_cast<size_t>(readLen)) {
                _tmp.close();
                _src.close();
                mutex_enter_blocking(&sdMu);
                sd.remove(kTempPath);
                mutex_exit(&sdMu);
                return fail(500, "could not write temp log");
            }
            return Status::Pending;
        }

        case State::Finalize: {
            _tmp.flush();
            _tmp.close();
            _src.close();

            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }
            sd.remove(MESSAGE_LOG_PATH);
            const bool renamed = sd.rename(kTempPath, MESSAGE_LOG_PATH);
            if (!renamed) {
                sd.remove(kTempPath);
            }
            mutex_exit(&sdMu);
            if (!renamed) {
                return fail(500, "could not replace log");
            }
            _statusCode = 204;
            _state = State::Done;
            return Status::Done;
        }

        case State::Done:
        case State::Error:
        default:
            return Status::Done;
    }
}
