#include "HttpRequest.h"

#include "HttpConnection.h"

void HttpRequest::send(int statusCode, const char *contentType, const std::string &body,
                       const char *extraHeaders) {
    if (_conn && (!_connAlive || *_connAlive)) {
        _conn->respondNow(statusCode, contentType, body, extraHeaders);
    }
}

void HttpRequest::hold(int statusCode, const char *contentType, const char *extraHeaders) {
    if (_conn && (!_connAlive || *_connAlive)) {
        _conn->hold(statusCode, contentType, extraHeaders);
    }
}

size_t HttpRequest::write(const uint8_t *data, size_t len) {
    if (_conn && (!_connAlive || *_connAlive)) {
        return _conn->writeHeld(data, len);
    }
    return 0;
}

void HttpRequest::done() {
    if (_conn && (!_connAlive || *_connAlive)) {
        _conn->doneHeld();
    }
}

bool HttpRequest::alive() const {
    if (_connAlive && !*_connAlive) {
        return false;
    }
    return _conn && _conn->isAlive();
}
