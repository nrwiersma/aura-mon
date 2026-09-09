//
// ImmediateResponse - a trivial HttpResponseProducer that wraps a
// pre-built status/content-type/body for handlers that already have their
// whole response in hand and don't need to stream it.
//

#pragma once

#include <cstring>
#include <string>
#include <utility>

#include "HttpResponseProducer.h"

class ImmediateResponse : public HttpResponseProducer {
public:
    ImmediateResponse(int statusCode, std::string contentType, std::string body)
        : _statusCode(statusCode), _contentType(std::move(contentType)), _body(std::move(body)) {}

    static ImmediateResponse error(int statusCode, std::string reason) {
        ImmediateResponse r(statusCode, "text/plain", "");
        r._errorReason = std::move(reason);
        return r;
    }

    Status produce(uint8_t *buf, size_t cap, size_t &written) override {
        if (!_errorReason.empty() && _offset == 0) {
            written = 0;
            return Status::Error;
        }

        size_t remaining = _body.size() - _offset;
        size_t n = remaining < cap ? remaining : cap;
        if (n > 0) {
            memcpy(buf, _body.data() + _offset, n);
        }
        _offset += n;
        written = n;
        return _offset >= _body.size() ? Status::Done : Status::Data;
    }

    int statusCode() const override { return _statusCode; }
    const char *contentType() const override { return _contentType.c_str(); }
    const char *errorReason() const override { return _errorReason.empty() ? nullptr : _errorReason.c_str(); }

private:
    int _statusCode;
    std::string _contentType;
    std::string _body;
    std::string _errorReason;
    size_t _offset = 0;
};
