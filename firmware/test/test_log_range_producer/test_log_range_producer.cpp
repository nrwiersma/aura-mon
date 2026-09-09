//
// Unit tests for LogRangeProducer against MockSD (single-file mode).
//

#include <unity.h>

#include <string>
#include <vector>

#include "../stubs/TestCore.h"
#include "../../src/log_range_producer.h"

namespace {

void setLogContent(const std::string &content) {
    sd.fileExists = true;
    if (sd.file) {
        delete sd.file;
    }
    auto *f = new FsFile();
    f->open = true;
    f->data->assign(content.begin(), content.end());
    sd.file = f;
}

// Drives produce() as HttpConnection would (via onWritable/onPollTick),
// collecting every byte streamed across however many calls it takes, until
// the producer reports Done or Error.
struct Result {
    HttpResponseProducer::Status status;
    std::string body;
    int dataCalls = 0;
};

Result runToCompletion(LogRangeProducer &p, size_t bufSize = 8) {
    Result result{};
    std::vector<uint8_t> buf(bufSize);
    size_t written = 0;
    int steps = 0;
    do {
        result.status = p.produce(buf.data(), buf.size(), written);
        if (result.status == HttpResponseProducer::Status::Data ||
            (result.status == HttpResponseProducer::Status::Done && written > 0)) {
            result.body.append(reinterpret_cast<char *>(buf.data()), written);
            result.dataCalls++;
        }
        steps++;
    } while ((result.status == HttpResponseProducer::Status::Pending ||
              result.status == HttpResponseProducer::Status::Data) &&
             steps < 10000);
    return result;
}

}  // namespace

void setUp() {
    sd.fileExists = false;
    if (sd.file) {
        delete sd.file;
        sd.file = nullptr;
    }
}

void tearDown() {}

// ============================================================================
// Missing log file
// ============================================================================

void test_missing_log_returns_404() {
    LogRangeProducer p(0, 0);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Error), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(404, p.statusCode());
}

// ============================================================================
// Whole file, no start/limit
// ============================================================================

void test_no_range_streams_whole_file() {
    setLogContent("hello world, this is the log");
    LogRangeProducer p(0, 0);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(200, p.statusCode());
    TEST_ASSERT_EQUAL_STRING("hello world, this is the log", result.body.c_str());
    // With an 8-byte buffer and a 29-byte body, streaming should have taken
    // several calls, confirming this doesn't just return everything at once.
    TEST_ASSERT_GREATER_THAN(1, result.dataCalls);
}

// ============================================================================
// Start offset only
// ============================================================================

void test_start_offset_skips_leading_bytes() {
    setLogContent("0123456789abcdef");
    LogRangeProducer p(10, 0);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_STRING("abcdef", result.body.c_str());
}

// ============================================================================
// Start offset beyond EOF -> empty 204
// ============================================================================

void test_start_offset_beyond_eof_returns_204_empty() {
    setLogContent("short");
    LogRangeProducer p(100, 0);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(204, p.statusCode());
    TEST_ASSERT_EQUAL_STRING("", result.body.c_str());
}

// ============================================================================
// Limit caps the number of bytes returned
// ============================================================================

void test_limit_caps_returned_bytes() {
    setLogContent("0123456789abcdef");
    LogRangeProducer p(0, 5);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_STRING("01234", result.body.c_str());
}

// ============================================================================
// Start + limit together
// ============================================================================

void test_start_and_limit_together() {
    setLogContent("0123456789abcdef");
    LogRangeProducer p(4, 3);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_STRING("456", result.body.c_str());
}

// ============================================================================
// Limit larger than remaining bytes just returns what's left
// ============================================================================

void test_limit_larger_than_remaining_returns_remaining() {
    setLogContent("abc");
    LogRangeProducer p(0, 1000);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_STRING("abc", result.body.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_missing_log_returns_404);
    RUN_TEST(test_no_range_streams_whole_file);
    RUN_TEST(test_start_offset_skips_leading_bytes);
    RUN_TEST(test_start_offset_beyond_eof_returns_204_empty);
    RUN_TEST(test_limit_caps_returned_bytes);
    RUN_TEST(test_start_and_limit_together);
    RUN_TEST(test_limit_larger_than_remaining_returns_remaining);
    return UNITY_END();
}
