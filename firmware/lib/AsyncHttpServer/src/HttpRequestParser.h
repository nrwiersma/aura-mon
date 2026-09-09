//
// HttpRequestParser - incremental HTTP/1.1 request parser.
//
// Feed it arbitrary byte chunks as they arrive off the wire (they may be
// split at any byte boundary, including mid-header-line or mid-body) and it
// accumulates a HttpRequest, returning Complete once the whole request
// (request line + headers + Content-Length body, if any) has been parsed.
//
// Deliberately has zero socket/lwIP dependency so it can be unit tested with
// plain byte buffers.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "HttpRequest.h"

enum class HttpParseStatus {
    NeedMoreData,
    Complete,
    Error,
};

class HttpRequestParser {
public:
    HttpRequestParser() { reset(); }

    // Resets the parser to parse a brand new request (e.g. after a
    // keep-alive request has been fully handled).
    void reset();

    // Feeds `len` more bytes received from the transport. May be called
    // repeatedly; returns the parser's status after consuming this chunk.
    // Once Complete or Error is returned, further calls are ignored until
    // reset() is called.
    HttpParseStatus feed(const uint8_t *data, size_t len);

    // Valid once feed() has returned Complete.
    const HttpRequest &request() const { return _req; }

    // Valid once feed() has returned Error.
    const char *errorReason() const { return _errorReason; }

    // Fired exactly once, right after the request line + headers have been
    // fully parsed (method/path/query/headers are final; the body, if any,
    // has not been touched yet). Lets a caller decide - based on the now-
    // known method/path/headers - whether to redirect the body elsewhere
    // via streamBodyTo() before any body bytes are consumed.
    using HeadersCompleteCallback = std::function<void(const HttpRequest &)>;
    void onHeadersComplete(HeadersCompleteCallback cb) { _onHeadersComplete = std::move(cb); }

    // Redirects all remaining body bytes to `sink` instead of buffering
    // them into request().body. Must be called synchronously from the
    // onHeadersComplete() callback. Bypasses kMaxBodyBytes entirely -
    // callers that stream a body are responsible for their own bounds
    // (e.g. a fixed maximum firmware image size).
    using BodySink = std::function<void(const uint8_t *, size_t)>;
    void streamBodyTo(BodySink sink) { _bodySink = std::move(sink); }

    // Maximum number of bytes this parser will buffer for the request
    // line + headers before giving up with an error (guards against a
    // client that never sends a terminating blank line).
    static constexpr size_t kMaxHeaderBytes = 4096;

    // Maximum body size accepted via Content-Length, unless the body has
    // been redirected via streamBodyTo().
    static constexpr size_t kMaxBodyBytes = 65536;

private:
    enum class State {
        RequestLine,
        Headers,
        Body,
        Complete,
        Error,
    };

    HttpParseStatus fail(const char *reason);
    HttpParseStatus pump();
    bool consumeLine(std::string &line);
    bool parseRequestLine(const std::string &line);
    bool parseHeaderLine(const std::string &line);
    static void parseQueryString(const std::string &qs, HttpRequest &req);
    static std::string urlDecode(const std::string &s);

    State _state = State::RequestLine;
    std::string _buf;         // unconsumed bytes accumulated so far
    size_t _consumed = 0;     // read cursor into _buf
    HttpRequest _req;
    size_t _contentLength = 0;
    size_t _bodyBytesRemaining = 0;
    HeadersCompleteCallback _onHeadersComplete;
    BodySink _bodySink;
    const char *_errorReason = nullptr;
};
