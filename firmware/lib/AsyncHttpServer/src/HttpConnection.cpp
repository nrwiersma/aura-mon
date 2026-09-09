#include "HttpConnection.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#ifndef UNIT_TEST
#include <Arduino.h>
#include <hardware/sync.h>
#endif

namespace {

#ifndef UNIT_TEST
// _outBuf is mutated from two contexts: c0Queue tasks (req.write/done on a
// held connection) and lwIP callbacks (tcp_sent/tcp_poll -> flush()). Those
// callbacks run from IRQ/async-context and can preempt a task mid-append,
// corrupting the std::string. Guard every _outBuf touchpoint by disabling
// interrupts briefly (correct primitive for loop-vs-IRQ on the same core;
// nests safely via save/restore).
struct IrqGuard {
    uint32_t state;
    IrqGuard() : state(save_and_disable_interrupts()) {}
    ~IrqGuard() { restore_interrupts(state); }
};
#else
struct IrqGuard {};
#endif

const char *statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown";
    }
}

// Case-insensitively extracts the boundary= parameter from a Content-Type
// header value, tolerating an optionally-quoted value. Returns "" if the
// header isn't multipart/form-data or has no boundary.
std::string extractBoundary(const std::string &contentType) {
    static const char kNeedle[] = "boundary=";
    size_t pos = std::string::npos;
    for (size_t i = 0; i + sizeof(kNeedle) - 1 <= contentType.size(); i++) {
        bool match = true;
        for (size_t j = 0; j < sizeof(kNeedle) - 1; j++) {
            if (std::tolower(static_cast<unsigned char>(contentType[i + j])) != kNeedle[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            pos = i + sizeof(kNeedle) - 1;
            break;
        }
    }
    if (pos == std::string::npos) {
        return "";
    }
    size_t end = contentType.find(';', pos);
    std::string value = contentType.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    if (!value.empty() && value.front() == '"') {
        size_t closing = value.find('"', 1);
        value = value.substr(1, closing == std::string::npos ? std::string::npos : closing - 1);
    }
    return value;
}

}  // namespace

HttpConnection::HttpConnection(HttpTransport &transport, HttpRouter &router)
    : _transport(transport), _router(router) {
    _parser.onHeadersComplete([this](const HttpRequest &req) { onHeadersComplete(req); });
    touch();
}

HttpConnection::~HttpConnection() {
    *_aliveFlag = false;
}

void HttpConnection::onHeadersComplete(const HttpRequest &req) {
    const HttpRouter::UploadFactoryFn *factory = _router.findUpload(req.method, req.path);
    if (!factory) {
        return;  // ordinary route - body (if any) buffers normally
    }

    _isUpload = true;
    const std::string *contentType = req.header("content-type");
    std::string boundary = extractBoundary(contentType ? *contentType : "");
    if (boundary.empty()) {
        _uploadBoundaryError = true;
        return;  // dispatch() responds 400 once the (buffered) body is complete
    }

    _uploadHandler = (*factory)(const_cast<HttpRequest &>(req));
    _multipart = std::make_unique<MultipartParser>(boundary, *this);
    _parser.streamBodyTo([this](const uint8_t *data, size_t len) { _multipart->feed(data, len); });
}

void HttpConnection::onPartBegin(const std::string &name, const std::string &filename) {
    _partOpen = true;
    if (_uploadHandler) {
        _uploadHandler->onUploadStart(name, filename);
    }
}

void HttpConnection::onPartData(const uint8_t *data, size_t len) {
    if (_uploadHandler) {
        _uploadHandler->onUploadWrite(data, len);
    }
}

void HttpConnection::onPartEnd() {
    _partOpen = false;
    if (_uploadHandler) {
        _uploadHandler->onUploadEnd();
    }
}

void HttpConnection::touch() {
#ifndef UNIT_TEST
    _lastActivityMs = millis();
#endif
}

uint32_t HttpConnection::millis32() const {
#ifdef UNIT_TEST
    return 0;
#else
    return millis();
#endif
}

void HttpConnection::onDataReceived(const uint8_t *data, size_t len) {
    touch();
    if (_state != State::AwaitingRequest) {
        // Ignore stray data once we've started responding (no pipelining
        // support - "Connection: close" always).
        return;
    }

    HttpParseStatus status = _parser.feed(data, len);
    if (status == HttpParseStatus::NeedMoreData) {
        return;
    }
    if (status == HttpParseStatus::Error) {
        HttpRequest &req = const_cast<HttpRequest &>(_parser.request());
        req._conn = this;
        req._connAlive = _aliveFlag;
        req.send(400, "text/plain", _parser.errorReason() ? _parser.errorReason() : "malformed request");
        _state = State::Responded;
        const IrqGuard irq;
        flush();  // close happens from reap() - never from this callback
        return;
    }

    dispatch();
}

void HttpConnection::dispatch() {
    HttpRequest &req = const_cast<HttpRequest &>(_parser.request());
    req._conn = this;
    req._connAlive = _aliveFlag;

    if (_isUpload) {
        if (_uploadBoundaryError || !_uploadHandler) {
            respondNow(400, "text/plain", "missing or invalid multipart boundary", nullptr);
        } else {
            if (!_multipart->isDone() || _partOpen) {
                // Body ended without a proper closing boundary/part.
                _uploadHandler->onUploadAborted();
            }
            _uploadHandler->finish(req);
            if (!_answered) {
                respondNow(500, "text/plain", "upload failed", nullptr);
            }
        }
        _uploadFinished = true;
        _state = State::Responded;
        const IrqGuard irq;
        flush();  // close happens from reap() - never from this callback
        return;
    }

    _router.route(req);

    if (!_answered) {
        // Handler responded neither inline nor via hold() - treat as a bug.
        respondNow(500, "text/plain", "handler produced no response", nullptr);
    }

    _state = _chunked ? State::Held : State::Responded;
    const IrqGuard irq;
    flush();  // close happens from reap() - never from this callback
}

void HttpConnection::respondNow(int statusCode, const char *contentType, const std::string &body,
                                const char *extraHeaders) {
    if (_answered || !isAlive()) {
        return;
    }
    _answered = true;

    const IrqGuard irq;

    char line[160];
    snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", statusCode, statusText(statusCode));
    _outBuf = line;
    _outBuf += "Content-Type: ";
    _outBuf += contentType;
    _outBuf += "\r\n";
    if (extraHeaders) {
        _outBuf += extraHeaders;
    }
    _outBuf += "Access-Control-Allow-Origin: *\r\n";
    _outBuf += "Connection: close\r\n";
    snprintf(line, sizeof(line), "Content-Length: %zu\r\n\r\n", body.size());
    _outBuf += line;
    _outBuf += body;

    if (_state == State::Responded) {
        const IrqGuard irq;
        flush();  // close happens from reap() - never from this callback
    }
}

void HttpConnection::hold(int statusCode, const char *contentType, const char *extraHeaders) {
    if (_answered || !isAlive()) {
        return;
    }
    _answered = true;
    _chunked = true;

    const IrqGuard irq;

    char line[160];
    snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", statusCode, statusText(statusCode));
    _outBuf = line;
    _outBuf += "Content-Type: ";
    _outBuf += contentType;
    _outBuf += "\r\n";
    if (extraHeaders) {
        _outBuf += extraHeaders;
    }
    _outBuf += "Access-Control-Allow-Origin: *\r\n";
    _outBuf += "Connection: close\r\n";
    _outBuf += "Transfer-Encoding: chunked\r\n\r\n";

    // Always attempt to flush the headers immediately: hold() runs before
    // dispatch() has flipped _state to Held, so waiting for that would mean
    // a handler that calls write() right after hold() always sees _outBuf
    // non-empty and always gets rejected. Flushing here lets that common
    // case succeed inline instead of always deferring to a retry.
    flush();
}

size_t HttpConnection::writeHeld(const uint8_t *data, size_t len) {
    if (!_chunked || !isAlive() || len == 0) {
        return 0;
    }
    // A record is either fully staged or not staged at all: while a
    // previous frame is still draining (outBuf non-empty) we refuse new
    // data so the caller retries the same record later. len is capped so a
    // single call always fits within one staged frame.
    const IrqGuard irq;  // the size check, append, and flush must be atomic
                         // against IRQ-context flush() draining _outBuf
    if (len > kMaxHeldWrite || _outBuf.size() > 0) {
        return 0;
    }
    touch();

    char sizeLine[16];
    const int hdrLen = snprintf(sizeLine, sizeof(sizeLine), "%zx\r\n", len);
    _outBuf.append(sizeLine, hdrLen);
    _outBuf.append(reinterpret_cast<const char *>(data), len);
    _outBuf += "\r\n";

    // Best-effort flush now. If the transport only accepts part of the
    // frame, those bytes are already irrevocably on the wire - the
    // remainder simply stays queued in _outBuf and will drain via a later
    // onWritable()/onPollTick() call. We must NOT discard it here: doing so
    // would both lose the unsent tail and let the caller believe nothing
    // was sent, causing it to resend the whole record and corrupt the
    // chunked stream with a duplicate. Either way the data is now safely
    // accounted for, so report full acceptance.
    flush();
    return len;
}

void HttpConnection::doneHeld() {
    if (!_chunked || !isAlive()) {
        return;
    }
    const IrqGuard irq;
    touch();
    _outBuf += "0\r\n\r\n";
    _state = State::Responded;  // nothing more is coming; reap() closes it
    flush();
}

void HttpConnection::onWritable() {
    const IrqGuard irq;
    flush();
}

void HttpConnection::onPollTick() {
    // Deliberately not touch()ed: tcp_poll fires continuously for every
    // open pcb, so it is not client activity - counting it would mask a
    // stalled client from the idle timeout forever.
    const IrqGuard irq;
    flush();
}

void HttpConnection::onClosed() {
    if (_isUpload && !_uploadFinished && _uploadHandler) {
        _uploadHandler->onUploadAborted();
        _uploadFinished = true;
    }
    _state = State::Closed;
}

void HttpConnection::flush() {
    // Callers must hold IrqGuard: _outBuf is shared between the loop task
    // (writeHeld/doneHeld) and IRQ-context callbacks (onWritable/onPollTick).
    if (_outBuf.empty()) {
        return;
    }
    size_t n = _transport.write(reinterpret_cast<const uint8_t *>(_outBuf.data()), _outBuf.size());
    if (n > 0) {
        _outBuf.erase(0, n);
    }
}

void HttpConnection::closeIfDrained() {
    // Runs from loop context only (reap()). Closing from inside an lwIP
    // callback RSTs the connection on this stack, so socket callbacks only
    // ever flush() - this is the sole closer.
    const IrqGuard irq;
    if (_state == State::Responded && _outBuf.empty()) {
        _transport.close();
        if (!_transport.isOpen()) {
            // close() detaches the lwIP callbacks, so onClosed() can never
            // fire for a locally-initiated close. Mark the connection done
            // immediately so reap() frees the slot on the next loop pass
            // instead of holding it for the full idle timeout.
            _state = State::Closed;
        }
        // else: lwIP couldn't queue the FIN yet (out of memory); the next
        // reap() pass retries the close.
    }
}
