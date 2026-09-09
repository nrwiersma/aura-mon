//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <HttpRequest.h>
#include <HttpResponseProducer.h>

// Serves a static file from the SD card's public/ directory, one bounded
// chunk at a time, including gzip-variant lookup and content-type-by-
// extension - the same behaviour as the previous not-found fallback, minus
// ever holding the SD card for longer than a single chunk's read.
class StaticFileProducer : public HttpResponseProducer {
public:
    StaticFileProducer(HttpMethod method, std::string uri);

    Status produce(uint8_t *buf, size_t cap, size_t &written) override;

    int statusCode() const override { return _statusCode; }
    const char *contentType() const override { return _contentType.c_str(); }
    const char *extraHeaders() const override { return _gzip ? "Content-Encoding: gzip\r\n" : nullptr; }
    const char *errorReason() const override { return _errorReason.empty() ? nullptr : _errorReason.c_str(); }

private:
    static constexpr size_t kChunkSize = 1024;

    enum class State {
        Start,
        Copy,
        Done,
        Error,
    };

    Status fail(int statusCode, const char *reason);
    static std::string contentTypeForPath(const std::string &path);

    State _state = State::Start;
    int _statusCode = 200;
    std::string _contentType = "text/plain";
    std::string _errorReason;
    bool _gzip = false;

    HttpMethod _method;
    std::string _uri;

    FsFile _src;
};
