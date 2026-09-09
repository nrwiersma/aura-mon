//
// FakeTransport - in-memory HttpTransport for unit testing HttpConnection
// without any lwIP/socket dependency. Supports simulating backpressure via
// a configurable "space available per write" cap.
//

#pragma once

#include <string>

#include "../../lib/AsyncHttpServer/src/HttpTransport.h"

class FakeTransport : public HttpTransport {
public:
    size_t write(const uint8_t *data, size_t len) override {
        if (!_open) {
            return 0;
        }
        size_t n = len;
        if (writeLimit > 0 && n > writeLimit) {
            n = writeLimit;
        }
        written.append(reinterpret_cast<const char *>(data), n);
        return n;
    }

    void close() override {
        if (failClose) {
            return;  // simulate lwIP being unable to queue the FIN (ERR_MEM)
        }
        _open = false;
        closeCalled = true;
    }

    bool isOpen() const override { return _open; }

    // Test-inspection surface.
    std::string written;
    size_t writeLimit = 0;  // 0 = unlimited
    bool closeCalled = false;
    bool failClose = false;  // close() becomes a no-op, forcing a retry later

private:
    bool _open = true;
};
