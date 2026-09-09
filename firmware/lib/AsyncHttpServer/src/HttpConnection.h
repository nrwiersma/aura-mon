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

class HttpConnection {
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
};
