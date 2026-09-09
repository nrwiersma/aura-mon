//
// MultipartParser - streaming multipart/form-data parser. Feed it raw body
// bytes (already separated from the request's own headers) as they arrive;
// it locates part boundaries and per-part header blocks itself and forwards
// only the sequence of Delegate callbacks below. It never buffers more than
// one boundary's worth of data at a time (mirroring the overlap-buffer
// technique used elsewhere in this codebase for bounded scanning), so a
// multi-megabyte part never needs to be buffered.
//
// Zero socket/lwIP dependency so it can be unit tested with plain byte
// buffers, the same way HttpRequestParser is.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class MultipartParser {
public:
    class Delegate {
    public:
        virtual ~Delegate() = default;

        // Content-Disposition's "name" and "filename" attributes (filename
        // is empty for a non-file form field).
        virtual void onPartBegin(const std::string &name, const std::string &filename) = 0;
        virtual void onPartData(const uint8_t *data, size_t len) = 0;
        virtual void onPartEnd() = 0;
    };

    MultipartParser(std::string boundary, Delegate &delegate);

    // Feeds more raw body bytes. Safe to call with arbitrarily-sized
    // chunks (including a single byte at a time); internal state carries
    // over between calls.
    void feed(const uint8_t *data, size_t len);

    bool hasError() const { return _state == State::Error; }
    bool isDone() const { return _state == State::Done; }

private:
    enum class State {
        Preamble,     // discard bytes before the first boundary
        PartHeaders,  // accumulating one part's header block
        PartData,     // streaming one part's body until the next boundary
        Done,
        Error,
    };

    static constexpr size_t kMaxPartHeaderBytes = 1024;

    void processBuffered();
    void handleHeaderBlock(const std::string &block);

    std::string _delimiter;       // "\r\n--" + boundary - precedes every part after the first
    std::string _firstDelimiter;  // "--" + boundary - starts the very first part
    Delegate &_delegate;
    State _state = State::Preamble;
    std::string _buf;
    bool _inPart = false;
};
