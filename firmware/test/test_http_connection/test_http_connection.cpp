//
// Unit tests for HttpConnection: immediate responses, streamed bodies,
// Pending retried via poll ticks, error-before-any-bytes, and backpressure.
//

#include <unity.h>

#include <cstring>
#include <vector>

#include "../../lib/AsyncHttpServer/src/HttpConnection.h"
#include "../../lib/AsyncHttpServer/src/HttpRouter.h"
#include "../../lib/AsyncHttpServer/src/HttpUploadHandler.h"
#include "../../lib/AsyncHttpServer/src/ImmediateResponse.h"
#include "../stubs/FakeTransport.h"

void setUp() {}
void tearDown() {}

static void feedRequest(HttpConnection &conn, const std::string &req) {
    conn.onDataReceived(reinterpret_cast<const uint8_t *>(req.data()), req.size());
}

// A producer that requires N poll ticks before it starts producing data,
// simulating a slow resource that eventually becomes ready without any
// hand-rolled pump loop.
class PendingThenDataProducer : public HttpResponseProducer {
public:
    explicit PendingThenDataProducer(int pendingTicks, std::string body)
        : _pendingTicks(pendingTicks), _body(std::move(body)) {}

    Status produce(uint8_t *buf, size_t cap, size_t &written) override {
        if (_pendingTicks > 0) {
            _pendingTicks--;
            written = 0;
            return Status::Pending;
        }
        size_t remaining = _body.size() - _offset;
        size_t n = remaining < cap ? remaining : cap;
        memcpy(buf, _body.data() + _offset, n);
        _offset += n;
        written = n;
        return _offset >= _body.size() ? Status::Done : Status::Data;
    }

private:
    int _pendingTicks;
    std::string _body;
    size_t _offset = 0;
};

class ErrorBeforeAnyBytesProducer : public HttpResponseProducer {
public:
    Status produce(uint8_t *, size_t, size_t &written) override {
        written = 0;
        return Status::Error;
    }
    const char *errorReason() const override { return "boom"; }
};

// ============================================================================
// Immediate (small, single-shot) responses
// ============================================================================

void test_immediate_response_sends_content_length_and_closes() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "application/json", "{\"ok\":true}");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /status HTTP/1.1\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("Content-Length: 11") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("{\"ok\":true}") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
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

// ============================================================================
// Streamed bodies (multiple Data calls)
// ============================================================================

void test_single_call_producer_uses_content_length_not_chunked() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/logs", [](const HttpRequest &) {
        return std::make_unique<PendingThenDataProducer>(0, "hello-world-body");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /logs HTTP/1.1\r\n\r\n");
    // The producer's whole body fits in one produce() call (work buffer is
    // 512B), so it resolves straight to Done on the first call - the
    // connection then knows the exact length up front and uses
    // Content-Length instead of chunked framing.
    TEST_ASSERT_TRUE(transport.written.find("Content-Length: 16") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("hello-world-body") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

class MultiChunkProducer : public HttpResponseProducer {
public:
    Status produce(uint8_t *buf, size_t cap, size_t &written) override {
        static const char *chunks[] = {"AAA", "BBB", "CCC"};
        if (_idx >= 3) {
            written = 0;
            return Status::Done;
        }
        size_t len = strlen(chunks[_idx]);
        memcpy(buf, chunks[_idx], len);
        written = len;
        _idx++;
        return Status::Data;
    }

private:
    int _idx = 0;
};

void test_multi_call_streamed_body_is_chunk_framed() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/stream", [](const HttpRequest &) {
        return std::make_unique<MultiChunkProducer>();
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /stream HTTP/1.1\r\n\r\n");
    // onDataReceived only pumps once per produce cycle per call in our
    // implementation's dispatch+pump; drive additional writable ticks to
    // pull the rest of the data through.
    for (int i = 0; i < 5 && !transport.closeCalled; i++) {
        conn.onWritable();
    }

    TEST_ASSERT_TRUE(transport.written.find("Transfer-Encoding: chunked") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("3\r\nAAA\r\n") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("3\r\nBBB\r\n") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("3\r\nCCC\r\n") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("0\r\n\r\n") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

// ============================================================================
// Pending retried via poll ticks (no hand-rolled pump loop needed)
// ============================================================================

void test_pending_producer_retried_via_poll_tick_without_new_data() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/trunc", [](const HttpRequest &) {
        return std::make_unique<PendingThenDataProducer>(3, "done");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /trunc HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(transport.written.empty());  // still pending, nothing sent

    conn.onPollTick();
    conn.onPollTick();
    TEST_ASSERT_TRUE(transport.written.empty());  // still pending

    conn.onPollTick();  // this call flips to Data/Done
    TEST_ASSERT_TRUE(transport.written.find("done") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

// ============================================================================
// Errors before any bytes sent
// ============================================================================

void test_error_before_any_bytes_maps_to_500() {
    FakeTransport transport;
    HttpRouter router;
    router.on(HttpMethod::GET, "/boom", [](const HttpRequest &) {
        return std::make_unique<ErrorBeforeAnyBytesProducer>();
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /boom HTTP/1.1\r\n\r\n");

    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 500") != std::string::npos);
    TEST_ASSERT_TRUE(transport.written.find("boom") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

// ============================================================================
// Backpressure: transport accepts partial writes
// ============================================================================

void test_backpressure_partial_writes_do_not_drop_or_duplicate_bytes() {
    FakeTransport transport;
    transport.writeLimit = 5;  // only accept 5 bytes per write() call
    HttpRouter router;
    router.on(HttpMethod::GET, "/status", [](const HttpRequest &) {
        return std::make_unique<ImmediateResponse>(200, "application/json", "{\"ok\":true}");
    });
    HttpConnection conn(transport, router);

    feedRequest(conn, "GET /status HTTP/1.1\r\n\r\n");
    // Not fully flushed yet due to the write cap.
    TEST_ASSERT_FALSE(transport.closeCalled);

    // Drain in small increments, exactly as repeated onWritable() calls
    // (driven by tcp_sent callbacks) would.
    for (int i = 0; i < 50 && !transport.closeCalled; i++) {
        conn.onWritable();
    }

    TEST_ASSERT_TRUE(transport.closeCalled);
    TEST_ASSERT_TRUE(transport.written.find("{\"ok\":true}") != std::string::npos);
    // Exactly one occurrence - no duplication.
    size_t first = transport.written.find("{\"ok\":true}");
    size_t second = transport.written.find("{\"ok\":true}", first + 1);
    TEST_ASSERT_EQUAL(std::string::npos, second);
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
    std::unique_ptr<HttpResponseProducer> finish() override {
        events.push_back("finish");
        return std::make_unique<ImmediateResponse>(200, "application/json", "{\"ok\":true}");
    }

    std::vector<std::string> events;
    std::string received;
};

void test_upload_route_streams_parts_to_handler_and_returns_finish_response() {
    FakeTransport transport;
    HttpRouter router;
    auto *handler = new RecordingUploadHandler();
    router.onUpload(HttpMethod::POST, "/ota", [handler](const HttpRequest &) {
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
    // Never aborted - the part closed normally before finish() was called.
    for (const auto &e : handler->events) {
        TEST_ASSERT_TRUE(e != "aborted");
    }
    TEST_ASSERT_TRUE(transport.written.find("HTTP/1.1 200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(transport.closeCalled);
}

void test_upload_missing_boundary_returns_400() {
    FakeTransport transport;
    HttpRouter router;
    router.onUpload(HttpMethod::POST, "/ota", [](const HttpRequest &) {
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
    router.onUpload(HttpMethod::POST, "/ota", [handler](const HttpRequest &) {
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
    router.onUpload(HttpMethod::POST, "/ota", [handler](const HttpRequest &) {
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
    RUN_TEST(test_immediate_response_sends_content_length_and_closes);
    RUN_TEST(test_not_found_route_returns_404);
    RUN_TEST(test_malformed_request_returns_400);
    RUN_TEST(test_single_call_producer_uses_content_length_not_chunked);
    RUN_TEST(test_multi_call_streamed_body_is_chunk_framed);
    RUN_TEST(test_pending_producer_retried_via_poll_tick_without_new_data);
    RUN_TEST(test_error_before_any_bytes_maps_to_500);
    RUN_TEST(test_backpressure_partial_writes_do_not_drop_or_duplicate_bytes);
    RUN_TEST(test_upload_route_streams_parts_to_handler_and_returns_finish_response);
    RUN_TEST(test_upload_missing_boundary_returns_400);
    RUN_TEST(test_upload_split_across_many_small_feeds_still_streams_correctly);
    RUN_TEST(test_upload_never_closed_calls_aborted_before_finish);
    return UNITY_END();
}
