//
// HttpRequest - the parsed result of an incoming request, produced
// incrementally by HttpRequestParser.
//

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

enum class HttpMethod {
    UNKNOWN,
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
};

HttpMethod httpMethodFromString(const std::string &s);

class HttpConnection;

struct HttpHeader {
    std::string name;  // lower-cased
    std::string value;
};

struct HttpQueryParam {
    std::string key;
    std::string value;
};

struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string path;                          // decoded, without query string
    std::vector<HttpQueryParam> query;
    std::vector<HttpHeader> headers;
    std::string body;

    // Returns the value of the first query parameter with the given key,
    // or nullptr if not present.
    const std::string *queryParam(const std::string &key) const {
        for (const auto &q : query) {
            if (q.key == key) {
                return &q.value;
            }
        }
        return nullptr;
    }

    // Returns the value of the first header with the given name (matched
    // case-insensitively against the caller-supplied lower-case name), or
    // nullptr if not present.
    const std::string *header(const std::string &lowerName) const {
        for (const auto &h : headers) {
            if (h.name == lowerName) {
                return &h.value;
            }
        }
        return nullptr;
    }

    // ---- Response channel ----
    // Handlers write their response through these. The common case is
    // send() (whole body in hand, done inline). A handler that needs to
    // stream slowly (e.g. an SD export driven by a task) instead calls
    // hold(), then write()/done() later from wherever it likes; the
    // connection stays parked in between.

    // Sends a complete response (headers + body + close) and finishes the
    // request. Safe no-op if the request was already answered or the
    // connection is gone.
    void send(int statusCode, const char *contentType, const std::string &body,
              const char *extraHeaders = nullptr);

    // Sends just the response headers (chunked body to follow) and parks
    // the connection: the handler may return without responding further,
    // and nothing will drive this request until write()/done() are called
    // (e.g. from a task). Socket callbacks stay inert while held.
    void hold(int statusCode, const char *contentType, const char *extraHeaders = nullptr);

    // Streams body bytes into a held response. Returns the number of
    // bytes accepted; a short count means the send window is full - the
    // caller should retry the unsent tail later.
    size_t write(const uint8_t *data, size_t len);
    size_t write(const char *str) { return write(reinterpret_cast<const uint8_t *>(str), strlen(str)); }

    // Ends a held response (chunked terminator, flush, close).
    void done();

    // False once the client has disconnected; a background producer should
    // abandon its work when this goes false.
    bool alive() const;

private:
    friend class HttpConnection;
    // Set by HttpConnection at dispatch; null for requests never routed
    // (e.g. parse errors). All response methods no-op when null.
    HttpConnection *_conn = nullptr;
    // Snapshotted alongside _conn at dispatch time. Lets every response
    // method safely detect "the connection is gone" even after HttpConnection
    // itself has been destroyed (e.g. reap()'s idle timeout racing a
    // still-running background job), without ever dereferencing _conn once
    // that's happened.
    std::shared_ptr<bool> _connAlive;
};
