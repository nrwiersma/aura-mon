//
// Unit tests for HttpConnection: inline responses via req.send, parked
// responses via req.hold/write/done, backpressure, and multipart uploads.
//

#include <unity.h>

#include <cstring>
#include <vector>

#include "../../lib/AsyncHttpServer/src/HttpConnection.h"
#include "../../lib/AsyncHttpServer/src/HttpRouter.h"
#include "../../lib/AsyncHttpServer/src/HttpUploadHandler.h"
#include "../stubs/FakeTransport.h"

void setUp() {}
void tearDown() {}

static void feedRequest(HttpConnection &conn, const std::string &req) {
    conn.onDataReceived(reinterpret_cast<const uint8_t *>(req.data()), req.size());
    // Emulate reap(): closes only ever happen from loop context, never from
    // the receive path.
    conn.closeIfDrained();
}

// ============================================================================
// Inline (small, single-shot) responses
// ============================================================================

void test_send_writes_full_response_and_closes() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [](HttpRequest &req) {
        req.send(200, "application/json", "{\"ok\":true}");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /status HTTP/1.1\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("Content-Length: 11") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("{\"ok\":true}") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
    // A completed inline response must free its slot immediately - locally
    // initiated closes never fire onClosed(), so without this the slot
    // would be held until the idle timeout and a few fast requests would
    // starve the server's (small) connection pool.
    TEST_ASSERT_TRUE(conn.isFinished());
}

void test_not_found_route_returns_404() {
    FakeTransport transport;
    HttpRouter router;
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /nope HTTP/1.1\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 404") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

void test_malformed_request_returns_400() {
    FakeTransport transport;
    HttpRouter router;
    HttpConnection conn(transport, router);

    feedRequest(conn, "GARBAGE\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 400") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

void test_handler_that_never_responds_gets_500() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/quiet", [](HttpRequest &) {});
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /quiet HTTP/1.1\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 500") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

// ============================================================================
// Held (deferred) responses: hold -> write* -> done
// ============================================================================

void test_hold_sends_headers_immediately_and_parks() {
    FakeTransport transport;
    HttpRouter router;
    HttpRequest *parked = nullptr;
    router.on(HttpMethod::GET, "/energy", [&parked](HttpRequest &req) {
        req.hold(200, "text/plain");
        parked = &req;
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /energy HTTP/1.1\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("Transfer-Encoding: chunked") != std::string::npos);
    TEST_ASSERT_FALSE(transport.closeCalled);  // parked, not closed
    TEST_ASSERT_NOT_NULL(parked);
    TEST_ASSERT_TRUE(parked->alive());
}

void test_held_write_is_chunk_framed_and_done_closes() {
    FakeTransport transport;
    HttpRouter router;
    HttpRequest *parked = nullptr;
    router.on(HttpMethod::GET, "/energy", [&parked](HttpRequest &req) {
        req.hold(200, "text/plain");
        parked = &req;
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /energy HTTP/1.1\r\n\r\n");

    const char *row = "1234,50.01\n";
    TEST_ASSERT_EQUAL(strlen(row), parked->write(reinterpret_cast<const uint8_t *>(row), strlen(row)));
    TEST_ASSERT_TRUE(transport.written.find("b\r\n1234,50.01\n\r\n") != std::string::npos);
    TEST_ASSERT_FALSE(transport.closeCalled);

    parked->done();
    TEST_ASSERT_TRUE(transport.written.find("0\r\n\r\n") != std::string::npos);
    conn.closeIfDrained();  // emulates reap(), the sole closer
    TEST_ASSERT_TRUE(transport.closeCalled);
}

void test_held_write_stages_data_even_when_window_only_partially_accepts_it() {
    FakeTransport transport;
    HttpRouter router;
    HttpRequest *parked = nullptr;
    router.on(HttpMethod::GET, "/energy", [&parked](HttpRequest &req) {
        req.hold(200, "text/plain");
        parked = &req;
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /energy HTTP/1.1\r\n\r\n");

    // The transport only accepts 1 byte per write() call, so writeHeld()'s
    // first flush attempt can only partially drain the staged frame. Those
    // bytes are already irrevocably on the wire, so the write must still be
    // reported as accepted (not rejected) - rejecting it here would either
    // lose the unflushed remainder or cause the caller to duplicate the
    // record once the window reopens.
    transport.writeLimit = 1;
    const char *row = "ROWDATA\n";
    size_t accepted = parked->write(reinterpret_cast<const uint8_t *>(row), strlen(row));
    TEST_ASSERT_EQUAL(strlen(row), accepted);

    // Nothing more can be staged until the partially-flushed frame drains.
    accepted = parked->write(reinterpret_cast<const uint8_t *>(row), strlen(row));
    TEST_ASSERT_EQUAL(0, accepted);

    // Draining via onWritable() (as repeated tcp_sent callbacks would)
    // flushes the rest of the first frame without losing or duplicating
    // any of it.
    transport.writeLimit = 0;
    for (int i = 0; i < 50; i++) {
        conn.onWritable();
    }
    size_t first = transport.written.find("ROWDATA");
    TEST_ASSERT_TRUE(first != std::string::npos);
    TEST_ASSERT_EQUAL(std::string::npos, transport.written.find("ROWDATA", first + 1));

    // Now that the frame is fully drained, a new write is accepted again.
    accepted = parked->write(reinterpret_cast<const uint8_t *>(row), strlen(row));
    TEST_ASSERT_EQUAL(strlen(row), accepted);
}

void test_alive_goes_false_after_peer_disconnect() {
    FakeTransport transport;
    HttpRouter router;
    HttpRequest *parked = nullptr;
    router.on(HttpMethod::GET, "/energy", [&parked](HttpRequest &req) {
        req.hold(200, "text/plain");
        parked = &req;
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /energy HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(parked->alive());

    conn.onClosed();
    TEST_ASSERT_FALSE(parked->alive());
    // write/done become no-ops once dead.
    TEST_ASSERT_EQUAL(0, parked->write(reinterpret_cast<const uint8_t *>("x"), 1));
}

// Regression test: AsyncHttpServer::reap() can destroy a HttpConnection
// (idle timeout) while a c0Queue job still holds its own copy of the
// HttpRequest. That copy must detect the connection is gone rather than
// dereferencing freed memory.
void test_alive_and_write_are_safe_after_connection_is_destroyed() {
    FakeTransport transport;
    HttpRouter router;
    HttpRequest captured;
    router.on(HttpMethod::GET, "/energy", [&captured](HttpRequest &req) {
        req.hold(200, "text/plain");
        captured = req;  // simulates a job stashing its own HttpRequest copy
    });
    auto *conn = new HttpConnection(transport, router);

    feedRequest(*conn, "GET /energy HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(captured.alive());

    // Simulate reap() tearing down the slot out from under the job.
    delete conn;

    TEST_ASSERT_FALSE(captured.alive());
    TEST_ASSERT_EQUAL(0, captured.write(reinterpret_cast<const uint8_t *>("x"), 1));
    captured.done();  // must also be a safe no-op, not a crash
}

void test_write_before_hold_is_rejected() {
    FakeTransport transport;
    HttpRouter router;
    HttpRequest *captured = nullptr;
    router.on(HttpMethod::GET, "/x", [&captured](HttpRequest &req) { captured = &req; });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /x HTTP/1.1\r\n\r\n");
    // No hold() called: write must be rejected (connection not in chunked mode).
    TEST_ASSERT_EQUAL(0, captured->write(reinterpret_cast<const uint8_t *>("x"), 1));
}

// Regression test: lwIP can fail to queue the FIN (ERR_MEM under pbuf
// pressure). The connection must stay open and retry the close on the next
// reap() pass instead of aborting - aborting RSTs a connection whose
// response was already fully queued, and the client sees
// ERR_CONNECTION_RESET on an otherwise-successful request.
void test_failed_close_is_retried_from_poll_not_aborted() {
    FakeTransport transport;
    transport.failClose = true;
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [](HttpRequest &req) {
        req.send(200, "application/json", "{\"ok\":true}");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /status HTTP/1.1\r\n\r\n");

    // Response fully written, but the close didn't land: still open, not
    // finished, and no data lost.
    TEST_ASSERT_TRUE(transport.written.find("{\"ok\":true}") != std::string::npos);
    TEST_ASSERT_FALSE(transport.closeCalled);
    TEST_ASSERT_TRUE(transport.isOpen());
    TEST_ASSERT_FALSE(conn.isFinished());

    // Once lwIP has memory again, the next reap() pass's retry closes it.
    transport.failClose = false;
    conn.closeIfDrained();
    TEST_ASSERT_TRUE(transport.closeCalled);
    TEST_ASSERT_TRUE(conn.isFinished());
}

// ============================================================================
// Backpressure on inline responses: transport accepts partial writes
// ============================================================================

void test_backpressure_partial_writes_do_not_drop_or_duplicate_bytes() {
    FakeTransport transport;
    transport.writeLimit = 5;  // only accept 5 bytes per write() call
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [](HttpRequest &req) {
        req.send(200, "application/json", "{\"ok\":true}");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /status HTTP/1.1\r\n\r\n");
    // Not fully flushed yet due to the write cap.
    TEST_ASSERT_FALSE(transport.closeCalled);

    // Drain in small increments, exactly as repeated onWritable() calls
    // (driven by tcp_sent callbacks) interleaved with reap() passes would.
    for (int i = 0; i < 50 && !transport.closeCalled; i++) {
        conn.onWritable();
        conn.closeIfDrained();
    }

    TEST_ASSERT_TRUE(transport.closeCalled);
    size_t first = transport.written.find("{\"ok\":true}");
    TEST_ASSERT_TRUE(first != std::string::npos);
    TEST_ASSERT_EQUAL(std::string::npos, transport.written.find("{\"ok\":true}", first + 1));
}

// ============================================================================
// Streaming multipart uploads
// ============================================================================

class RecordingUploadHandler : public HttpUploadHandler {
public:
    void onUploadStart(const std::string &name, const std::string &filename) override {
        events.push_back("start:" + name + ":" + filename);
    }
    void onUploadWrite(const uint8_t *data, size_t len) override {
        received.append(reinterpret_cast<const char *>(data), len);
        events.push_back("write");
    }
    void onUploadEnd() override { events.push_back("end"); }
    void onUploadAborted() override { events.push_back("aborted"); }
    void finish(HttpRequest &req) override {
        events.push_back("finish");
        req.send(200, "application/json", "{\"ok\":true}");
    }

    std::vector<std::string> events;
    std::string received;
};

void test_upload_route_streams_parts_to_handler_and_sends_finish_response() {
    FakeTransport transport;
    HttpRouter router;
    auto *handler = new RecordingUploadHandler();
    router.onUpload(HttpMethod::POST, "/ota", [handler](HttpRequest &) {
        return std::unique_ptr<HttpUploadHandler>(handler);
    });
    HttpConnection conn(transport, router);

    std::string body =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"firmware\"; filename=\"fw.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "BINARYDATA"
        "\r\n--B--\r\n";
    std::string req = "POST /ota HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=B\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body;
    feedRequest(conn, req);

    TEST_ASSERT_EQUAL_STRING("BINARYDATA", handler->received.c_str());
    TEST_ASSERT_TRUE(handler->events.size() >= 3);
    TEST_ASSERT_EQUAL_STRING("start:firmware:fw.bin", handler->events.front().c_str());
    TEST_ASSERT_EQUAL_STRING("finish", handler->events.back().c_str());
    for (const auto &e : handler->events) {
        TEST_ASSERT_TRUE(e != "aborted");
    }
    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

void test_upload_missing_boundary_returns_400() {
    FakeTransport transport;
    HttpRouter router;
    router.onUpload(HttpMethod::POST, "/ota", [](HttpRequest &) {
        return std::make_unique<RecordingUploadHandler>();
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "POST /ota HTTP/1.1\r\nContent-Type: multipart/form-data\r\nContent-Length: 4\r\n\r\nabcd");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 400") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

void test_upload_split_across_many_small_feeds_still_streams_correctly() {
    FakeTransport transport;
    HttpRouter router;
    auto *handler = new RecordingUploadHandler();
    router.onUpload(HttpMethod::POST, "/ota", [handler](HttpRequest &) {
        return std::unique_ptr<HttpUploadHandler>(handler);
    });
    HttpConnection conn(transport, router);

    std::string body =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"firmware\"; filename=\"fw.bin\"\r\n"
        "\r\n"
        "0123456789"
        "\r\n--B--\r\n";
    std::string req = "POST /ota HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=B\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body;
    for (size_t i = 0; i < req.size(); i++) {
        conn.onDataReceived(reinterpret_cast<const uint8_t *>(req.data() + i), 1);
    }

    TEST_ASSERT_EQUAL_STRING("0123456789", handler->received.c_str());
    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 200 OK") != std::string::npos);
}

void test_upload_never_closed_calls_aborted_before_finish() {
    FakeTransport transport;
    HttpRouter router;
    auto *handler = new RecordingUploadHandler();
    router.onUpload(HttpMethod::POST, "/ota", [handler](HttpRequest &) {
        return std::unique_ptr<HttpUploadHandler>(handler);
    });
    HttpConnection conn(transport, router);

    // Body ends (Content-Length exhausted) without ever sending a closing
    // boundary - a truncated/aborted upload.
    std::string body =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"firmware\"; filename=\"fw.bin\"\r\n"
        "\r\n"
        "partial-data-only";
    std::string req = "POST /ota HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=B\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body;
    feedRequest(conn, req);

    bool sawAborted = false;
    for (const auto &e : handler->events) {
        if (e == "aborted") sawAborted = true;
    }
    TEST_ASSERT_TRUE(sawAborted);
    TEST_ASSERT_EQUAL_STRING("finish", handler->events.back().c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_send_writes_full_response_and_closes);
    RUN_TEST(test_not_found_route_returns_404);
    RUN_TEST(test_malformed_request_returns_400);
    RUN_TEST(test_handler_that_never_responds_gets_500);
    RUN_TEST(test_hold_sends_headers_immediately_and_parks);
    RUN_TEST(test_held_write_is_chunk_framed_and_done_closes);
    RUN_TEST(test_held_write_stages_data_even_when_window_only_partially_accepts_it);
    RUN_TEST(test_alive_goes_false_after_peer_disconnect);
    RUN_TEST(test_alive_and_write_are_safe_after_connection_is_destroyed);
    RUN_TEST(test_write_before_hold_is_rejected);
    RUN_TEST(test_failed_close_is_retried_from_poll_not_aborted);
    RUN_TEST(test_backpressure_partial_writes_do_not_drop_or_duplicate_bytes);
    RUN_TEST(test_upload_route_streams_parts_to_handler_and_sends_finish_response);
    RUN_TEST(test_upload_missing_boundary_returns_400);
    RUN_TEST(test_upload_split_across_many_small_feeds_still_streams_correctly);
    RUN_TEST(test_upload_never_closed_calls_aborted_before_finish);
    return UNITY_END();
}
