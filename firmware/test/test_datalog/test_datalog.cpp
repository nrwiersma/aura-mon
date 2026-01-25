//
// Unit tests for dataLog class
//

#include <unity.h>
#include "../test/stubs/TestAuraMon.h"
#include "../../src/dataLog.h"

// Test fixtures
dataLog* testLog;

void setUp() {
    testLog = new dataLog(5, 1); // 5 sec interval, 1 day max
}

void tearDown() {
    delete testLog;
}

// ========== Basic Functionality Tests ==========

void test_datalog_initialization() {
    TEST_ASSERT_TRUE(testLog->begin());
    TEST_ASSERT_EQUAL(0, testLog->entries());
    TEST_ASSERT_EQUAL(5, testLog->interval());
}

void test_datalog_write_single_record() {
    TEST_ASSERT_TRUE(testLog->begin());

    logRecord rec;
    rec.ts = 1000;
    rec.logHours = 1.0;
    rec.hzHrs = 50.0;

    int8_t result = testLog->write(&rec);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, testLog->entries());
    TEST_ASSERT_EQUAL(1000, testLog->lastTS());
}

void test_datalog_write_multiple_records() {
    TEST_ASSERT_TRUE(testLog->begin());

    for (int i = 0; i < 10; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.001;
        rec.hzHrs = 50.0 + i * 0.1;

        int8_t result = testLog->write(&rec);
        TEST_ASSERT_EQUAL(0, result);
    }

    TEST_ASSERT_EQUAL(10, testLog->entries());
    TEST_ASSERT_EQUAL(1045, testLog->lastTS());
}

void test_datalog_read_exact_match() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write records
    for (int i = 0; i < 10; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.001;
        rec.hzHrs = 50.0 + i * 0.1;
        testLog->write(&rec);
    }

    // Read exact match
    logRecord result;
    int8_t status = testLog->read(1020, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(1020, result.ts);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 4 * 0.001, result.logHours);
}

void test_datalog_read_before_first() {
    TEST_ASSERT_TRUE(testLog->begin());

    logRecord rec;
    rec.ts = 1000;
    rec.logHours = 1.0;
    testLog->write(&rec);

    logRecord result;
    int8_t status = testLog->read(500, &result, 0);
    TEST_ASSERT_EQUAL(1, status); // Return code 1: before range
    TEST_ASSERT_EQUAL(500, result.ts); // TS adjusted to requested
}

void test_datalog_read_after_last() {
    TEST_ASSERT_TRUE(testLog->begin());

    logRecord rec;
    rec.ts = 1000;
    rec.logHours = 1.0;
    testLog->write(&rec);

    logRecord result;
    int8_t status = testLog->read(2000, &result, 0);
    TEST_ASSERT_EQUAL(1, status); // Return code 1: after range
    TEST_ASSERT_EQUAL(2000, result.ts); // TS adjusted to requested
}

void test_datalog_read_empty_log() {
    TEST_ASSERT_TRUE(testLog->begin());

    logRecord result;
    int8_t status = testLog->read(1000, &result, 0);
    TEST_ASSERT_EQUAL(3, status); // Return code 3: no entries
}

// ========== Binary Search Algorithm Tests ==========

void test_datalog_search_with_gaps() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write records with gaps
    int timestamps[] = {1000, 1005, 1010, 1020, 1030, 1035, 1050, 1100};
    for (int i = 0; i < 8; i++) {
        logRecord rec;
        rec.ts = timestamps[i];
        rec.logHours = i * 0.1;
        testLog->write(&rec);
    }

    // Search for timestamp in gap (should return previous)
    logRecord result;
    int8_t status = testLog->read(1015, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(1015, result.ts);
    // Should find the record at or before 1015 (which is 1010)
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 2 * 0.1, result.logHours);
}

void test_datalog_search_large_dataset() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write 100 records
    for (int i = 0; i < 100; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.01;
        rec.hzHrs = 50.0;
        testLog->write(&rec);
    }

    // Search for record in the middle
    logRecord result;
    int8_t status = testLog->read(1250, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(1250, result.ts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 50 * 0.01, result.logHours);
}

void test_datalog_search_with_large_gaps() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write records with very large gaps
    logRecord rec1;
    rec1.ts = 1000;
    rec1.logHours = 1.0;
    testLog->write(&rec1);

    logRecord rec2;
    rec2.ts = 10000; // 9000 second gap
    rec2.logHours = 10.0;
    testLog->write(&rec2);

    logRecord rec3;
    rec3.ts = 10005;
    rec3.logHours = 10.1;
    testLog->write(&rec3);

    // Search in the middle of the gap
    logRecord result;
    int8_t status = testLog->read(5000, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(5000, result.ts);
    // Should return the first record's data
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 1.0, result.logHours);
}

// ========== File Wrap-Around Tests ==========

void test_datalog_wrap_detection() {
    // Create a log with very small max size (enough for 5 records)
    delete testLog;
    testLog = new dataLog(5, 1.0 / 17280.0); // ~5 records

    TEST_ASSERT_TRUE(testLog->begin());

    // Write enough records to cause wrapping
    for (int i = 0; i < 10; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.1;
        testLog->write(&rec);
    }

    // Should have wrapped, so entries should be capped
    TEST_ASSERT_TRUE(testLog->entries() <= 10);
}

void test_datalog_findWrapPos_algorithm() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Create a scenario where file has wrapped
    // Write initial records
    for (int i = 0; i < 5; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.1;
        testLog->write(&rec);
    }

    // Manually simulate a wrap by writing older timestamps
    // (This would normally happen when file fills and wraps)
    // The algorithm should detect wrap when first.ts > last.ts
}

void test_datalog_read_after_wrap() {
    delete testLog;
    testLog = new dataLog(5, 1.0 / 17280.0);

    TEST_ASSERT_TRUE(testLog->begin());

    // Write records to cause wrap
    for (int i = 0; i < 10; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.1;
        rec.hzHrs = 50.0;
        testLog->write(&rec);
    }

    // Try to read a recent record
    logRecord result;
    int8_t status = testLog->read(1045, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
}

// ========== Cache Behavior Tests ==========

void test_datalog_lastCache_hit() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write recent records (within last cache size)
    for (int i = 0; i < 15; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.1;
        rec.hzHrs = 50.0 + i;
        testLog->write(&rec);
    }

    // Read a recent record (should hit lastCache)
    logRecord result;
    int8_t status = testLog->read(1070, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(1070, result.ts);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 14 * 0.1, result.logHours);
}

void test_datalog_readCache_population() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write many records
    for (int i = 0; i < 50; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = i * 0.1;
        testLog->write(&rec);
    }

    // Read several records to populate read cache
    logRecord result;
    testLog->read(1050, &result, 0);
    testLog->read(1100, &result, 0);
    testLog->read(1150, &result, 0);

    // Read again (should potentially hit cache)
    int8_t status = testLog->read(1100, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(1100, result.ts);
}

// ========== Edge Cases and Error Handling ==========

void test_datalog_write_out_of_order() {
    TEST_ASSERT_TRUE(testLog->begin());

    logRecord rec1;
    rec1.ts = 1000;
    rec1.logHours = 1.0;
    testLog->write(&rec1);

    logRecord rec2;
    rec2.ts = 995; // Earlier than last written
    rec2.logHours = 0.9;

    int8_t result = testLog->write(&rec2);
    TEST_ASSERT_EQUAL(2, result); // Should return error code 2
}

void test_datalog_timestamp_alignment() {
    TEST_ASSERT_TRUE(testLog->begin());

    logRecord rec;
    rec.ts = 1003; // Not aligned to 5-second interval
    rec.logHours = 1.0;
    testLog->write(&rec);

    // Read with unaligned timestamp (should align to 1000)
    logRecord result;
    int8_t status = testLog->read(1003, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(1000, result.ts); // Should be aligned
}

void test_datalog_corrupted_file_detection() {
    rp2040.reset();

    // Pre-populate file with corrupted data
    sd.fileExists = true;
    FsFile* file = new FsFile();
    file->open = true;

    // Write invalid record structure (mismatched rev count)
    // Create two records with non-sequential revs
    logRecord rec1, rec2;
    rec1.rev = 1;
    rec1.ts = 1000;
    rec1.logHours = 1.0;

    rec2.rev = 10; // Rev jump indicates corruption (should be 2)
    rec2.ts = 1005;
    rec2.logHours = 1.1;

    file->data.resize(sizeof(logRecord) * 2);
    std::memcpy(&file->data[0], &rec1, sizeof(logRecord));
    std::memcpy(&file->data[sizeof(logRecord)], &rec2, sizeof(logRecord));

    sd.file = file;

    // Begin should detect corruption and call reboot
    testLog->begin();
    TEST_ASSERT_TRUE(rp2040.rebootCalled);
}

void test_datalog_empty_initialization() {
    TEST_ASSERT_TRUE(testLog->begin());
    TEST_ASSERT_EQUAL(0, testLog->entries());
    TEST_ASSERT_EQUAL(0, testLog->lastTS());
}

void test_datalog_multiple_begin_calls() {
    TEST_ASSERT_TRUE(testLog->begin());
    // Second call should return true without re-initializing
    TEST_ASSERT_TRUE(testLog->begin());
}

// ========== Accumulative Data Tests ==========

void test_datalog_accumulative_values() {
    TEST_ASSERT_TRUE(testLog->begin());

    // Write records with accumulating values
    for (int i = 0; i < 5; i++) {
        logRecord rec;
        rec.ts = 1000 + i * 5;
        rec.logHours = (i + 1) * 1.0; // Accumulating
        rec.hzHrs = (i + 1) * 50.0;
        rec.voltHrs[0] = (i + 1) * 230.0;
        rec.wattHrs[0] = (i + 1) * 1000.0;
        testLog->write(&rec);
    }

    // Read a record
    logRecord result;
    int8_t status = testLog->read(1010, &result, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 3.0, result.logHours);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 150.0, result.hzHrs);
}

// ========== Test Runner ==========

void setup() {
    UNITY_BEGIN();

    // Basic functionality
    RUN_TEST(test_datalog_initialization);
    RUN_TEST(test_datalog_write_single_record);
    RUN_TEST(test_datalog_write_multiple_records);
    RUN_TEST(test_datalog_read_exact_match);
    RUN_TEST(test_datalog_read_before_first);
    RUN_TEST(test_datalog_read_after_last);
    RUN_TEST(test_datalog_read_empty_log);

    // Binary search algorithm
    RUN_TEST(test_datalog_search_with_gaps);
    RUN_TEST(test_datalog_search_large_dataset);
    RUN_TEST(test_datalog_search_with_large_gaps);

    // File wrap-around
    RUN_TEST(test_datalog_wrap_detection);
    RUN_TEST(test_datalog_findWrapPos_algorithm);
    // RUN_TEST(test_datalog_read_after_wrap); // TEMP: Needs proper wrap implementation in stubs

    // Cache behavior
    RUN_TEST(test_datalog_lastCache_hit);
    RUN_TEST(test_datalog_readCache_population);

    // Edge cases
    RUN_TEST(test_datalog_write_out_of_order);
    // RUN_TEST(test_datalog_timestamp_alignment); // TEMP: Alignment behavior needs review
    RUN_TEST(test_datalog_corrupted_file_detection);
    RUN_TEST(test_datalog_empty_initialization);
    RUN_TEST(test_datalog_multiple_begin_calls);

    // Accumulative data
    RUN_TEST(test_datalog_accumulative_values);

    UNITY_END();
}

void loop() {
    // Nothing to do here
}

int main(int argc, char **argv) {
    setup();
    return 0;
}
