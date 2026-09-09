//
// Unit tests for LogTruncateProducer against MockSD (multi-file mode, so a
// source file and a temp file can exist independently at once).
//

#include <unity.h>

#include <string>

#include "../stubs/TestCore.h"
#include "../../src/log_truncate_producer.h"

namespace {

constexpr const char *kLogPath = MESSAGE_LOG_PATH;
constexpr const char *kTempPath = MESSAGE_LOG_PATH ".trunc";

void setLogContent(const std::string &content) {
    sd.multiFileMode = true;
    auto *f = new FsFile();
    f->data->assign(content.begin(), content.end());
    if (sd.namedFiles.count(kLogPath)) {
        delete sd.namedFiles[kLogPath];
    }
    sd.namedFiles[kLogPath] = f;
    sd.namedFileExists[kLogPath] = true;
}

std::string readNamedFile(const char *path) {
    auto it = sd.namedFiles.find(path);
    if (it == sd.namedFiles.end()) {
        return "";
    }
    return std::string(reinterpret_cast<const char *>(it->second->data->data()), it->second->data->size());
}

// Runs produce() in a loop (as HttpConnection would via onPollTick/
// onWritable) until it stops returning Pending.
HttpResponseProducer::Status runToCompletion(LogTruncateProducer &p, int maxSteps = 10000) {
    uint8_t buf[64];
    size_t written = 0;
    HttpResponseProducer::Status status;
    int steps = 0;
    do {
        status = p.produce(buf, sizeof(buf), written);
        steps++;
    } while (status == HttpResponseProducer::Status::Pending && steps < maxSteps);
    return status;
}

}  // namespace

void setUp() {
    sd.multiFileMode = true;
    for (auto &kv : sd.namedFiles) {
        delete kv.second;
    }
    sd.namedFiles.clear();
    sd.namedFileExists.clear();
}

void tearDown() {}

// ============================================================================
// Missing log file
// ============================================================================

void test_missing_log_returns_404() {
    LogTruncateProducer p;
    auto status = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Error), static_cast<int>(status));
    TEST_ASSERT_EQUAL(404, p.statusCode());
}

// ============================================================================
// No restart marker present - whole file is preserved unchanged
// ============================================================================

void test_no_marker_present_preserves_whole_file() {
    setLogContent("just some plain log lines\nwith no markers at all\n");
    LogTruncateProducer p;
    auto status = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(status));
    TEST_ASSERT_EQUAL(204, p.statusCode());
    TEST_ASSERT_EQUAL_STRING("just some plain log lines\nwith no markers at all\n", readNamedFile(kLogPath).c_str());
}

// ============================================================================
// Marker present - everything before the last marker is dropped
// ============================================================================

void test_marker_found_truncates_up_to_marker() {
    std::string before = "old boot log line 1\nold boot log line 2\n";
    std::string marker = "**** RESTART ****";
    std::string after = "\nnew boot log line 1\nnew boot log line 2\n";
    setLogContent(before + marker + after);

    LogTruncateProducer p;
    auto status = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(status));
    TEST_ASSERT_EQUAL(204, p.statusCode());
    TEST_ASSERT_EQUAL_STRING((marker + after).c_str(), readNamedFile(kLogPath).c_str());
}

void test_only_last_marker_is_kept_as_boundary() {
    std::string marker = "**** RESTART ****";
    std::string content = "one\n" + marker + "two\n" + marker + "three\n";
    setLogContent(content);

    LogTruncateProducer p;
    auto status = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(status));
    TEST_ASSERT_EQUAL_STRING((marker + "three\n").c_str(), readNamedFile(kLogPath).c_str());
}

// ============================================================================
// Marker spanning a chunk boundary (chunk size is 1024 bytes)
// ============================================================================

void test_marker_spanning_chunk_boundary_is_still_found() {
    std::string marker = "**** RESTART ****";
    // Pad so the marker starts a few bytes before the 1024-byte chunk
    // boundary, forcing the backward scan to stitch it together via the
    // overlap buffer across two chunk reads.
    std::string before(1024 - 5, 'a');
    std::string after = "tail-content-after-marker";
    setLogContent(before + marker + after);

    LogTruncateProducer p;
    auto status = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(status));
    TEST_ASSERT_EQUAL_STRING((marker + after).c_str(), readNamedFile(kLogPath).c_str());
}

// ============================================================================
// Bounded steps: every call before Done reports Pending (no body to stream)
// ============================================================================

void test_intermediate_calls_report_pending_not_data() {
    std::string marker = "**** RESTART ****";
    setLogContent(std::string(2500, 'x') + marker + "keep-me");

    LogTruncateProducer p;
    uint8_t buf[64];
    size_t written = 0;
    int pendingCount = 0;
    HttpResponseProducer::Status status;
    do {
        status = p.produce(buf, sizeof(buf), written);
        if (status == HttpResponseProducer::Status::Pending) {
            pendingCount++;
            TEST_ASSERT_EQUAL(0, written);
        }
    } while (status == HttpResponseProducer::Status::Pending);

    TEST_ASSERT_GREATER_THAN(1, pendingCount);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(status));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_missing_log_returns_404);
    RUN_TEST(test_no_marker_present_preserves_whole_file);
    RUN_TEST(test_marker_found_truncates_up_to_marker);
    RUN_TEST(test_only_last_marker_is_kept_as_boundary);
    RUN_TEST(test_marker_spanning_chunk_boundary_is_still_found);
    RUN_TEST(test_intermediate_calls_report_pending_not_data);
    return UNITY_END();
}
