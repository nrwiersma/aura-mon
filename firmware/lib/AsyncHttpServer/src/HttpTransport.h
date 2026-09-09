//
// HttpTransport - the seam between the async HTTP connection logic and the
// underlying socket implementation. Keeps HttpConnection/HttpRequestParser
// fully unit-testable without lwIP.
//

#pragma once

#include <cstddef>
#include <cstdint>

class HttpTransport {
public:
    virtual ~HttpTransport() = default;

    // Attempts to write `len` bytes. May accept fewer than `len` bytes if
    // the underlying send buffer is full (backpressure); the caller must
    // retry the remaining bytes once the transport signals writability
    // again (e.g. via a "sent"/poll callback owned by the caller).
    virtual size_t write(const uint8_t *data, size_t len) = 0;

    // Closes the connection. Safe to call multiple times.
    virtual void close() = 0;

    // Whether the connection is still open for writing.
    virtual bool isOpen() const = 0;
};
