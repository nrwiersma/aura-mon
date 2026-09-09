//
// HttpConnection - per-accepted-socket state machine. Owns the request
// parser for one connection. Handlers write responses through the
// HttpRequest response channel: small routes answer inline during
// dispatch (req.send), a long-running route parks the connection
// (req.hold) and drives it later from a task (req.write/done). Socket
// callbacks never drive application work - they only flush staged output
// and notice disconnects, so a connection always has exactly one driver.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "HttpRequestParser.h"
#include "HttpRouter.h"
#include "HttpTransport.h"
#include "HttpUploadHandler.h"
#include "MultipartParser.h"

class HttpConnection : public MultipartParser::Delegate {
public:
    HttpConnection(HttpTransport &transport, HttpRouter &router);
    ~HttpConnection() override;

    // Feed newly received bytes from the transport.
    void onDataReceived(const uint8_t *data, size_t len);

    // The transport has freed up send buffer space; flush staged output.
    void onWritable();

    // Periodic tick (e.g. lwIP tcp_poll); flushes staged output.
    void onPollTick();

    // The transport reports the connection has been closed.
    void onClosed();

    // Whether this connection has finished (fully written + closed) and its
    // slot can be reused for a new accepted connection.
    bool isFinished() const { return _state == State::Closed; }

    // Millis timestamp of the last activity (data received / bytes
    // written). The server uses this to reclaim slots from clients that
    // stall mid-request.
    uint32_t lastActivityMs() const { return _lastActivityMs; }

    // ---- Response channel (called via HttpRequest) ----

    // Complete response, sent at dispatch time.
    void respondNow(int statusCode, const char *contentType, const std::string &body,
                    const char *extraHeaders);

    // Headers now (chunked body to follow via writeHeld/doneHeld), then
    // the connection parks: no further driving from socket callbacks.
    void hold(int statusCode, const char *contentType, const char *extraHeaders);

    // Stage body bytes into a held response; returns bytes accepted.
    // All-or-nothing per call (0 = window full, retry the whole record
    // later); len is capped at kMaxHeldWrite so a single call always fits
    // one TCP send buffer.
    size_t writeHeld(const uint8_t *data, size_t len);

    // Finish a held response (terminator, flush, close).
    void doneHeld();

    // False once the peer is gone.
    bool isAlive() const { return _state != State::Closed; }

    // Close the connection once the staged response has fully flushed.
    // Only ever called from loop context (AsyncHttpServer::reap(), or tests
    // emulating it): tcp_close() from inside the lwIP recv callback resets
    // the connection on this stack (observed on-device as
    // ERR_CONNECTION_RESET on every inline response), so closes never
    // happen from socket callbacks - only flushes do.
    void closeIfDrained();

    // One writeHeld() call may stage at most this many body bytes, so it
    // always fits within a drained TCP send buffer.
    static constexpr size_t kMaxHeldWrite = 4096;

    // A shared flag that HttpRequest snapshots at dispatch time and checks
    // before every response-channel call. reap() can destroy this
    // HttpConnection (e.g. idle timeout) while a c0Queue job still holds a
    // HttpRequest& to it; the flag lets that job's next alive()/write()
    // call detect the connection is gone WITHOUT dereferencing freed
    // memory (the flag cell itself outlives the connection as long as the
    // job holds a reference to it). Flipped false in the destructor.
    std::shared_ptr<bool> aliveFlag() const { return _aliveFlag; }

private:
    enum class State {
        AwaitingRequest,
        Responded,  // response staged by the handler; flushing (and possibly retrying a failed close)
        Held,       // parked after hold(); driven by whoever called hold()
        Closed,
    };

    void dispatch();
    void flush();
    void onHeadersComplete(const HttpRequest &req);
    void touch();
    uint32_t millis32() const;

    // MultipartParser::Delegate - forwards each part to the active upload
    // handler as its data streams in off the wire.
    void onPartBegin(const std::string &name, const std::string &filename) override;
    void onPartData(const uint8_t *data, size_t len) override;
    void onPartEnd() override;

    HttpTransport &_transport;
    HttpRouter &_router;
    HttpRequestParser _parser;
    State _state = State::AwaitingRequest;
    bool _chunked = false;
    bool _answered = false;  // a response method already ran
    std::string _outBuf;
    uint32_t _lastActivityMs = 0;

    // Streaming upload state (set only for routes registered via
    // HttpRouter::onUpload()).
    bool _isUpload = false;
    bool _uploadBoundaryError = false;
    bool _uploadFinished = false;  // finish()/onUploadAborted() already called
    bool _partOpen = false;        // a part's onPartBegin fired without a matching onPartEnd yet
    std::unique_ptr<HttpUploadHandler> _uploadHandler;
    std::unique_ptr<MultipartParser> _multipart;

    std::shared_ptr<bool> _aliveFlag = std::make_shared<bool>(true);
};
