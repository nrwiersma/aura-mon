//
// HttpResponseProducer - unifies producers that stream a body over several
// calls and producers that must do background work before they can respond
// at all, behind one interface driven by HttpConnection whenever the
// transport is ready for more output.
//

#pragma once

#include <cstddef>
#include <cstdint>

class HttpResponseProducer {
public:
    enum class Status {
        Pending,  // no decision/data yet; nothing sent - call again later
        Data,     // `written` bytes of body content placed in the buffer
        Done,     // response complete; any final `written` bytes are the last chunk
        Error,    // only valid before any bytes have been sent; see errorReason()
    };

    virtual ~HttpResponseProducer() = default;

    // Called whenever the connection is ready for more output. Implementers
    // must do at most one bounded unit of work and return promptly -
    // never block.
    virtual Status produce(uint8_t *buf, size_t cap, size_t &written) = 0;

    virtual int statusCode() const { return 200; }
    virtual const char *contentType() const { return "text/plain"; }
    // Only meaningful when produce() returns Error.
    virtual const char *errorReason() const { return nullptr; }
};
