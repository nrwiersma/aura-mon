//
// HttpConnection - per-accepted-socket state machine. Owns the request
// parser and response producer for one connection, driven entirely by
// callbacks from the transport (data received / writable / poll tick /
// closed) - never blocks, never spins.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "HttpRequestParser.h"
#include "HttpResponseProducer.h"
#include "HttpRouter.h"
#include "HttpTransport.h"
#include "HttpUploadHandler.h"
#include "MultipartParser.h"

class HttpConnection : public MultipartParser::Delegate {
public:
    HttpConnection(HttpTransport &transport, HttpRouter &router);

    // Feed newly received bytes from the transport.
    void onDataReceived(const uint8_t *data, size_t len);

    // The transport has freed up send buffer space; try to make progress.
    void onWritable();

    // Periodic tick (e.g. lwIP tcp_poll) - lets a producer that is still
    // Pending retry without requiring new socket data.
    void onPollTick();

    // The transport reports the connection has been closed.
    void onClosed();

    // Whether this connection has finished (fully written + closed) and its
    // slot can be reused for a new accepted connection.
    bool isFinished() const { return _state == State::Closed; }

    // Millis timestamp of the last activity (data received / socket
    // writable). The server uses this to reclaim slots from clients that
    // stall mid-request.
    uint32_t lastActivityMs() const { return _lastActivityMs; }

private:
    enum class State {
        AwaitingRequest,
        Dispatched,
        Closing,
        Closed,
    };

    void dispatch();
    void pump();
    void writePending();
    void respondWithError(int statusCode, const char *reason);
    void appendHeaders(int statusCode, const char *contentType, bool chunked, size_t contentLength);
    void appendChunk(const uint8_t *data, size_t len);
    void appendChunkTerminator();
    void closeIfDrained();
    void onHeadersComplete(const HttpRequest &req);
    void touch();

    // MultipartParser::Delegate - forwards each part to the active upload
    // handler as its data streams in off the wire.
    void onPartBegin(const std::string &name, const std::string &filename) override;
    void onPartData(const uint8_t *data, size_t len) override;
    void onPartEnd() override;

    static constexpr size_t kWorkBufSize = 512;

    HttpTransport &_transport;
    HttpRouter &_router;
    HttpRequestParser _parser;
    std::unique_ptr<HttpResponseProducer> _producer;
    State _state = State::AwaitingRequest;
    bool _headersSent = false;
    bool _chunked = false;
    bool _finished = false;  // producer has emitted its last byte
    std::string _outBuf;
    uint8_t _workBuf[kWorkBufSize];

    // Streaming upload state (set only for routes registered via
    // HttpRouter::onUpload()).
    bool _isUpload = false;
    bool _uploadBoundaryError = false;
    bool _uploadFinished = false;  // finish()/onUploadAborted() already called
    bool _partOpen = false;        // a part's onPartBegin fired without a matching onPartEnd yet
    std::unique_ptr<HttpUploadHandler> _uploadHandler;
    std::unique_ptr<MultipartParser> _multipart;
    uint32_t _lastActivityMs = 0;
};
