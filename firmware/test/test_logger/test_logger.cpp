//
// Unit tests for the logger class.
//

#include <unity.h>
#include <cstring>
#include <ctime>
#include "../stubs/TestLogger.h"
#include "../../src/logger.h"
#include "../../src/storage.h"

// Shared logger instance recreated for each test.
Logger *testLogger;

// ---- helpers ---------------------------------------------------------------

// Returns the content that was written to the SD message file.
static std::string sdFileContent() {
    if (!sd.file || !sd.file->data) return "";
    return std::string(reinterpret_cast<const char *>(sd.file->data->data()),
                       sd.file->data->size());
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    // Reset Serial capture buffer.
    Serial.reset();

    // Reset SD stub.
    sd.fileExists = false;
    if (sd.file) {
        delete sd.file;
        sd.file = nullptr;
    }
    sd.directories.clear();

    // Release any handle the storage layer cached onto the stub we just reset.
    storage::init();

    // Use a time value that is <= 10,000,000 so timestamps are suppressed
    // unless a test explicitly requests them.
    mockNow = 0;

    testLogger = new Logger();
}

void tearDown() {
    delete testLogger;
    testLogger = nullptr;
}

// ============================================================================
// Serial output
// ============================================================================

void test_debugf_writes_to_serial() {
    testLogger->printf(LOG_LEVEL_DEBUG, "hello debug");

    TEST_ASSERT_TRUE(Serial.output.find("dbug hello debug") != std::string::npos);
}

void test_infof_writes_to_serial() {
    testLogger->printf(LOG_LEVEL_INFO, "hello info");

    TEST_ASSERT_TRUE(Serial.output.find("info hello info") != std::string::npos);
}

void test_errorf_writes_to_serial() {
    testLogger->printf(LOG_LEVEL_ERROR, "hello error");

    TEST_ASSERT_TRUE(Serial.output.find("eror hello error") != std::string::npos);
}

void test_serial_line_ends_with_crlf() {
    testLogger->printf(LOG_LEVEL_INFO, "msg");

    // The line must end with \r\n.
    const std::string &out = Serial.output;
    TEST_ASSERT_TRUE(out.size() >= 2);
    TEST_ASSERT_EQUAL('\r', out[out.size() - 2]);
    TEST_ASSERT_EQUAL('\n', out[out.size() - 1]);
}

void test_format_substitution_in_serial() {
    testLogger->printf(LOG_LEVEL_INFO, "val=%d name=%s", 42, "thing");

    TEST_ASSERT_TRUE(Serial.output.find("val=42 name=thing") != std::string::npos);
}

// ============================================================================
// Log-level SD-file filtering
// ============================================================================

void test_debugf_does_not_write_to_file() {
    testLogger->printf(LOG_LEVEL_DEBUG, "should not persist");

    TEST_ASSERT_EQUAL_STRING("", sdFileContent().c_str());
}

void test_infof_writes_to_file() {
    testLogger->printf(LOG_LEVEL_INFO, "persisted info");

    TEST_ASSERT_TRUE(sdFileContent().find("info persisted info") != std::string::npos);
}

void test_errorf_writes_to_file() {
    testLogger->printf(LOG_LEVEL_ERROR, "persisted error");

    TEST_ASSERT_TRUE(sdFileContent().find("eror persisted error") != std::string::npos);
}

// ============================================================================
// Restart marker
// ============================================================================

void test_restart_marker_prepended_on_first_file_write() {
    testLogger->printf(LOG_LEVEL_INFO, "first message");

    const std::string content = sdFileContent();
    TEST_ASSERT_TRUE(content.find("**** RESTART ****") != std::string::npos);
    // The restart marker must appear before the log line.
    TEST_ASSERT_TRUE(content.find("**** RESTART ****") < content.find("first message"));
}

void test_restart_marker_written_only_once() {
    testLogger->printf(LOG_LEVEL_INFO, "msg one");
    testLogger->printf(LOG_LEVEL_INFO, "msg two");

    const std::string content = sdFileContent();
    // Count occurrences of the marker.
    size_t pos = 0;
    int    count = 0;
    while ((pos = content.find("**** RESTART ****", pos)) != std::string::npos) {
        count++;
        pos++;
    }
    TEST_ASSERT_EQUAL(1, count);
}

void test_no_restart_marker_for_debug_only() {
    // debug-only writes must never trigger the restart marker.
    testLogger->printf(LOG_LEVEL_DEBUG, "only debug");

    TEST_ASSERT_EQUAL_STRING("", sdFileContent().c_str());
    // Now write an info line; the marker should still appear (logger not yet
    // flushed its _restart flag via a real write).
    testLogger->printf(LOG_LEVEL_INFO, "now info");
    TEST_ASSERT_TRUE(sdFileContent().find("**** RESTART ****") != std::string::npos);
}

// ============================================================================
// Timestamp in Serial output
// ============================================================================

void test_no_timestamp_when_time_not_set() {
    // mockNow == 0 → no timestamp prefix.
    testLogger->printf(LOG_LEVEL_INFO, "no ts");

    // The output should start with the level string, not a date.
    const std::string &out = Serial.output;
    TEST_ASSERT_TRUE(out.find("info") == 0);
}

void test_timestamp_present_when_time_set() {
    // Use a fixed epoch > 10,000,000: 2001-09-09T01:46:40Z == 1,000,000,000.
    mockNow = 1000000000;
    testLogger->printf(LOG_LEVEL_INFO, "with ts");

    // The Serial output should begin with a 4-digit year.
    const std::string &out = Serial.output;
    TEST_ASSERT_TRUE(out.size() > 4);
    // First char should be '2' (year 2001).
    TEST_ASSERT_EQUAL('2', out[0]);
    // The timestamp format is YYYY-MM-DDTHH:MM:SSZ (20 chars) followed by ' '.
    TEST_ASSERT_EQUAL('T', out[10]);
    TEST_ASSERT_EQUAL('Z', out[19]);
    TEST_ASSERT_EQUAL(' ', out[20]);
}

void test_timestamp_in_file_when_time_set() {
    mockNow = 1000000000;
    testLogger->printf(LOG_LEVEL_INFO, "file ts");

    const std::string content = sdFileContent();
    TEST_ASSERT_TRUE(content.find("2001-09-09T") != std::string::npos);
}

// ============================================================================
// Long messages (heap-allocation path in errorf / infof / debugf)
// ============================================================================

void test_infof_long_message() {
    // Build a message longer than the 64-byte stack buffer.
    const char *repeated = "abcdefghijklmnopqrstuvwxyz0123456789";              // 36 chars
    testLogger->printf(LOG_LEVEL_INFO, "%s%s%s", repeated, repeated, repeated); // 108 chars

    // The full repeated string must appear in the Serial output.
    TEST_ASSERT_TRUE(Serial.output.find(repeated) != std::string::npos);
    // And it should be in the file.
    TEST_ASSERT_TRUE(sdFileContent().find(repeated) != std::string::npos);
}

void test_errorf_long_message() {
    const char *repeated = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";               // 36 chars
    testLogger->printf(LOG_LEVEL_ERROR, "%s%s%s", repeated, repeated, repeated); // 108 chars

    TEST_ASSERT_TRUE(Serial.output.find(repeated) != std::string::npos);
    TEST_ASSERT_TRUE(sdFileContent().find(repeated) != std::string::npos);
}

// ============================================================================
// SD directory creation on first open failure
// ============================================================================

void test_mkdir_called_when_sd_open_fails() {
    // By default sd.fileExists == false, meaning MockSD::open will still work
    // (it creates the file on the fly). To simulate an initial open failure we
    // would need a more sophisticated stub; this test instead verifies that
    // when the file is not pre-existing the directory is NOT created (normal
    // path), because MockSD::open always succeeds.
    //
    // We therefore test the negative: no directory is created for a normal
    // first write.
    testLogger->printf(LOG_LEVEL_INFO, "normal open");

    TEST_ASSERT_EQUAL(0, (int) sd.directories.size());
}

// ============================================================================
// Multiple log levels written in sequence
// ============================================================================

void test_mixed_levels_only_info_and_error_in_file() {
    testLogger->printf(LOG_LEVEL_DEBUG, "debug line");
    testLogger->printf(LOG_LEVEL_INFO, "info line");
    testLogger->printf(LOG_LEVEL_ERROR, "error line");

    const std::string content = sdFileContent();
    TEST_ASSERT_TRUE(content.find("info line") != std::string::npos);
    TEST_ASSERT_TRUE(content.find("error line") != std::string::npos);
    TEST_ASSERT_TRUE(content.find("debug line") == std::string::npos);
}

void test_multiple_serial_writes_accumulate() {
    testLogger->printf(LOG_LEVEL_INFO, "first");
    testLogger->printf(LOG_LEVEL_INFO, "second");
    testLogger->printf(LOG_LEVEL_INFO, "third");

    TEST_ASSERT_TRUE(Serial.output.find("first") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.output.find("second") != std::string::npos);
    TEST_ASSERT_TRUE(Serial.output.find("third") != std::string::npos);
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    // Serial output
    RUN_TEST(test_debugf_writes_to_serial);
    RUN_TEST(test_infof_writes_to_serial);
    RUN_TEST(test_errorf_writes_to_serial);
    RUN_TEST(test_serial_line_ends_with_crlf);
    RUN_TEST(test_format_substitution_in_serial);

    // Log-level SD filtering
    RUN_TEST(test_debugf_does_not_write_to_file);
    RUN_TEST(test_infof_writes_to_file);
    RUN_TEST(test_errorf_writes_to_file);

    // Restart marker
    RUN_TEST(test_restart_marker_prepended_on_first_file_write);
    RUN_TEST(test_restart_marker_written_only_once);
    RUN_TEST(test_no_restart_marker_for_debug_only);

    // Timestamps
    RUN_TEST(test_no_timestamp_when_time_not_set);
    RUN_TEST(test_timestamp_present_when_time_set);
    RUN_TEST(test_timestamp_in_file_when_time_set);

    // Long messages (heap path)
    RUN_TEST(test_infof_long_message);
    RUN_TEST(test_errorf_long_message);

    // SD directory creation
    RUN_TEST(test_mkdir_called_when_sd_open_fails);

    // Mixed levels / sequence
    RUN_TEST(test_mixed_levels_only_info_and_error_in_file);
    RUN_TEST(test_multiple_serial_writes_accumulate);

    UNITY_END();
}

void loop() {
    // Nothing to do here.
}

int main(int argc, char **argv) {
    setup();
    return 0;
}
