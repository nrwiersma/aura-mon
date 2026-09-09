//
// Created by Nicholas Wiersma on 2026/01/23.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "log_range_producer.h"
#endif

#include "log_range_producer.h"

LogRangeProducer::LogRangeProducer(uint32_t startOffset, uint32_t limitBytes)
    : _startOffset(startOffset), _limitBytes(limitBytes) {}

HttpResponseProducer::Status LogRangeProducer::fail(int statusCode, const char *reason) {
    _statusCode = statusCode;
    _errorReason = reason;
    _state = State::Error;
    return Status::Error;
}

HttpResponseProducer::Status LogRangeProducer::produce(uint8_t *buf, size_t cap, size_t &written) {
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
            if (!_src) {
                mutex_exit(&sdMu);
                return fail(500, "could not open log");
            }

            const size_t fileSize = _src.size();
            if (_startOffset >= fileSize) {
                _src.close();
                mutex_exit(&sdMu);
                _statusCode = 204;
                _state = State::Done;
                return Status::Done;
            }
            if (_startOffset > 0 && !_src.seek(_startOffset)) {
                _src.close();
                mutex_exit(&sdMu);
                return fail(500, "could not seek log");
            }

            _remaining = fileSize - _startOffset;
            if (_limitBytes > 0 && _limitBytes < _remaining) {
                _remaining = _limitBytes;
            }
            mutex_exit(&sdMu);

            if (_remaining == 0) {
                _src.close();
                _statusCode = 204;
                _state = State::Done;
                return Status::Done;
            }

            _state = State::Copy;
            return Status::Pending;
        }

        case State::Copy: {
            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }
            const size_t toRead = min(_remaining, cap);
            const int readLen = _src.read(buf, toRead);
            mutex_exit(&sdMu);

            if (readLen <= 0) {
                _src.close();
                _state = State::Done;
                return Status::Done;
            }

            written = static_cast<size_t>(readLen);
            _remaining -= written;
            if (_remaining == 0) {
                _src.close();
                _state = State::Done;
                return Status::Done;
            }
            return Status::Data;
        }

        case State::Done:
        case State::Error:
        default:
            return Status::Done;
    }
}
