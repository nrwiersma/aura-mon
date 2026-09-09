#include "HttpConnection.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#ifndef UNIT_TEST
#include <Arduino.h>
#endif

#include "ImmediateResponse.h"

namespace {

const char *statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
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

    _uploadHandler = (*factory)(req);
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

void HttpConnection::onDataReceived(const uint8_t *data, size_t len) {
    touch();
    if (_state != State::AwaitingRequest) {
        // Ignore stray data once we've started responding (no pipelining
        // support in phase 1 - "Connection: close" always).
        return;
    }

    HttpParseStatus status = _parser.feed(data, len);
    if (status == HttpParseStatus::NeedMoreData) {
        return;
    }
    if (status == HttpParseStatus::Error) {
        respondWithError(400, _parser.errorReason() ? _parser.errorReason() : "malformed request");
        return;
    }

    dispatch();
    pump();
}

void HttpConnection::dispatch() {
    if (_isUpload) {
        if (_uploadBoundaryError || !_uploadHandler) {
            _producer = std::make_unique<ImmediateResponse>(
                ImmediateResponse::error(400, "missing or invalid multipart boundary"));
        } else {
            if (!_multipart->isDone() || _partOpen) {
                // Body ended without a proper closing boundary/part.
                _uploadHandler->onUploadAborted();
            }
            _producer = _uploadHandler->finish();
            if (!_producer) {
                _producer = std::make_unique<ImmediateResponse>(ImmediateResponse::error(500, "upload failed"));
            }
        }
        _uploadFinished = true;
        _state = State::Dispatched;
        return;
    }

    _producer = _router.route(_parser.request());
    if (!_producer) {
        _producer = std::make_unique<ImmediateResponse>(404, "application/json", "{\"error\":\"Not Found\"}");
    }
    _state = State::Dispatched;
}

void HttpConnection::respondWithError(int statusCode, const char *reason) {
    _producer = std::make_unique<ImmediateResponse>(ImmediateResponse::error(statusCode, reason ? reason : ""));
    _state = State::Dispatched;
    pump();
}

void HttpConnection::onWritable() {
    touch();
    pump();
}

void HttpConnection::onPollTick() {
    // Deliberately not touch()ed: tcp_poll fires continuously for every
    // open pcb, so it is not client activity - counting it would mask a
    // stalled client from the idle timeout forever. Poll still drives
    // pump() so Pending producers keep making progress.
    pump();
}

void HttpConnection::onClosed() {
    if (_isUpload && !_uploadFinished && _uploadHandler) {
        _uploadHandler->onUploadAborted();
        _uploadFinished = true;
    }
    _state = State::Closed;
}

void HttpConnection::writePending() {
    if (_outBuf.empty()) {
        return;
    }
    size_t n = _transport.write(reinterpret_cast<const uint8_t *>(_outBuf.data()), _outBuf.size());
    if (n > 0) {
        _outBuf.erase(0, n);
    }
}

void HttpConnection::appendHeaders(int statusCode, const char *contentType, bool chunked, size_t contentLength) {
    char line[160];
    snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", statusCode, statusText(statusCode));
    _outBuf += line;
    _outBuf += "Content-Type: ";
    _outBuf += contentType;
    _outBuf += "\r\n";
    if (_producer) {
        if (const char *extra = _producer->extraHeaders()) {
            _outBuf += extra;
        }
    }
    _outBuf += "Access-Control-Allow-Origin: *\r\n";
    _outBuf += "Connection: close\r\n";
    if (chunked) {
        _outBuf += "Transfer-Encoding: chunked\r\n\r\n";
    } else {
        snprintf(line, sizeof(line), "Content-Length: %zu\r\n\r\n", contentLength);
        _outBuf += line;
    }
}

void HttpConnection::appendChunk(const uint8_t *data, size_t len) {
    if (len == 0) {
        return;
    }
    char sizeLine[16];
    snprintf(sizeLine, sizeof(sizeLine), "%zx\r\n", len);
    _outBuf += sizeLine;
    _outBuf.append(reinterpret_cast<const char *>(data), len);
    _outBuf += "\r\n";
}

void HttpConnection::appendChunkTerminator() {
    _outBuf += "0\r\n\r\n";
}

void HttpConnection::closeIfDrained() {
    if (_finished && _outBuf.empty()) {
        _transport.close();
        _state = State::Closing;
    }
}

void HttpConnection::pump() {
    if (_state != State::Dispatched) {
        return;
    }

    writePending();
    if (!_outBuf.empty()) {
        // Still backed up from a previous round; wait for onWritable().
        return;
    }

    if (_finished) {
        closeIfDrained();
        return;
    }

    size_t written = 0;
    HttpResponseProducer::Status status = _producer->produce(_workBuf, kWorkBufSize, written);

    switch (status) {
        case HttpResponseProducer::Status::Pending:
            // Nothing to send yet; onPollTick() will retry.
            return;

        case HttpResponseProducer::Status::Error:
            if (!_headersSent) {
                std::string body = std::string("{\"error\":\"") +
                                    (_producer->errorReason() ? _producer->errorReason() : "internal error") +
                                    "\"}";
                int code = _producer->statusCode();
                if (code < 400) {
                    code = 500;  // errorReason() implies failure; default if the producer didn't set one
                }
                appendHeaders(code, "application/json", false, body.size());
                _outBuf += body;
                _headersSent = true;
            }
            // If headers were already sent we cannot report an error anymore;
            // just stop and close below.
            _finished = true;
            break;

        case HttpResponseProducer::Status::Data:
            if (!_headersSent) {
                appendHeaders(_producer->statusCode(), _producer->contentType(), /*chunked=*/true, 0);
                _chunked = true;
                _headersSent = true;
            }
            if (_chunked) {
                appendChunk(_workBuf, written);
            } else {
                _outBuf.append(reinterpret_cast<const char *>(_workBuf), written);
            }
            break;

        case HttpResponseProducer::Status::Done:
            if (!_headersSent) {
                // Single-shot response: exact length known now.
                appendHeaders(_producer->statusCode(), _producer->contentType(), /*chunked=*/false, written);
                _outBuf.append(reinterpret_cast<const char *>(_workBuf), written);
                _headersSent = true;
            } else if (_chunked) {
                appendChunk(_workBuf, written);
                appendChunkTerminator();
            } else {
                _outBuf.append(reinterpret_cast<const char *>(_workBuf), written);
            }
            _finished = true;
            break;
    }

    writePending();
    closeIfDrained();
}
