//
// Unit tests for logQueue: FIFO ordering, record copying, full queue
// behaviour and wrapping.
//

#include <unity.h>
#include "../stubs/TestCore.h"
#include "../../src/log_queue.h"

// ---- fixtures --------------------------------------------------------------

static logQueue *q;

void setUp() {
    q = new logQueue();
}

void tearDown() {
    delete q;
}

static LogRecord makeRecord(uint32_t ts) {
    LogRecord rec;
    rec.ts = ts;
    rec.logHours = 1.5;
    rec.hzHrs = 0.5;
    rec.voltHrs[0] = 2.5;
    rec.wattHrs[0] = 3.5;
    rec.vaHrs[0] = 4.5;
    return rec;
}

// ============================================================================
// Empty queue
// ============================================================================

void test_empty_queue_has_no_size() {
    TEST_ASSERT_EQUAL_UINT32(0, q->size());
}

void test_pop_on_empty_queue_returns_false() {
    LogRecord rec;
    TEST_ASSERT_FALSE(q->pop(&rec));
}

// ============================================================================
// Push and pop
// ============================================================================

void test_push_then_pop_returns_record() {
    auto in = makeRecord(100);
    TEST_ASSERT_TRUE(q->push(&in));
    TEST_ASSERT_EQUAL_UINT32(1, q->size());

    LogRecord out;
    TEST_ASSERT_TRUE(q->pop(&out));
    TEST_ASSERT_EQUAL_UINT32(100, out.ts);
    TEST_ASSERT_EQUAL_DOUBLE(1.5, out.logHours);
    TEST_ASSERT_EQUAL_DOUBLE(2.5, out.voltHrs[0]);
    TEST_ASSERT_EQUAL_UINT32(0, q->size());
}

void test_record_is_copied_into_queue() {
    auto in = makeRecord(100);
    q->push(&in);

    // The queued record is a copy, so the caller can reuse their record.
    in.ts = 200;
    in.logHours = 9.5;
    in.hzHrs = 9.5;
    in.voltHrs[0] = 9.5;
    in.wattHrs[0] = 9.5;
    in.vaHrs[0] = 9.5;

    LogRecord out;
    q->pop(&out);
    TEST_ASSERT_EQUAL_UINT32(100, out.ts);
    TEST_ASSERT_EQUAL_DOUBLE(1.5, out.logHours);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, out.hzHrs);
    TEST_ASSERT_EQUAL_DOUBLE(2.5, out.voltHrs[0]);
    TEST_ASSERT_EQUAL_DOUBLE(3.5, out.wattHrs[0]);
    TEST_ASSERT_EQUAL_DOUBLE(4.5, out.vaHrs[0]);
}

void test_popped_record_is_a_copy() {
    auto in = makeRecord(100);
    q->push(&in);

    LogRecord out;
    q->pop(&out);

    // Changing the popped record must not affect a record queued after it.
    out.ts = 300;
    q->push(&in);

    LogRecord next;
    q->pop(&next);
    TEST_ASSERT_EQUAL_UINT32(100, next.ts);
}

void test_records_pop_in_order() {
    for (uint32_t i = 1; i <= 3; i++) {
        auto rec = makeRecord(i * 10);
        TEST_ASSERT_TRUE(q->push(&rec));
    }

    LogRecord out;
    for (uint32_t i = 1; i <= 3; i++) {
        TEST_ASSERT_TRUE(q->pop(&out));
        TEST_ASSERT_EQUAL_UINT32(i * 10, out.ts);
    }
    TEST_ASSERT_FALSE(q->pop(&out));
}

// ============================================================================
// Full queue
// ============================================================================

void test_push_on_full_queue_returns_false() {
    for (uint32_t i = 0; i < LOG_QUEUE_SIZE; i++) {
        auto rec = makeRecord(i);
        TEST_ASSERT_TRUE(q->push(&rec));
    }
    TEST_ASSERT_EQUAL_UINT32(LOG_QUEUE_SIZE, q->size());

    auto rec = makeRecord(LOG_QUEUE_SIZE);
    TEST_ASSERT_FALSE(q->push(&rec));
    TEST_ASSERT_EQUAL_UINT32(LOG_QUEUE_SIZE, q->size());

    // The oldest record is kept, the new one is rejected.
    LogRecord out;
    TEST_ASSERT_TRUE(q->pop(&out));
    TEST_ASSERT_EQUAL_UINT32(0, out.ts);
}

void test_queue_wraps_and_keeps_order() {
    LogRecord out;

    // Fill, drain half, then fill again to force the indexes to wrap.
    for (uint32_t i = 0; i < LOG_QUEUE_SIZE; i++) {
        auto rec = makeRecord(i);
        q->push(&rec);
    }
    for (uint32_t i = 0; i < LOG_QUEUE_SIZE / 2; i++) {
        TEST_ASSERT_TRUE(q->pop(&out));
        TEST_ASSERT_EQUAL_UINT32(i, out.ts);
    }
    for (uint32_t i = 0; i < LOG_QUEUE_SIZE / 2; i++) {
        auto rec = makeRecord(LOG_QUEUE_SIZE + i);
        TEST_ASSERT_TRUE(q->push(&rec));
    }

    TEST_ASSERT_EQUAL_UINT32(LOG_QUEUE_SIZE, q->size());
    for (uint32_t i = LOG_QUEUE_SIZE / 2; i < LOG_QUEUE_SIZE + LOG_QUEUE_SIZE / 2; i++) {
        TEST_ASSERT_TRUE(q->pop(&out));
        TEST_ASSERT_EQUAL_UINT32(i, out.ts);
    }
    TEST_ASSERT_FALSE(q->pop(&out));
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_empty_queue_has_no_size);
    RUN_TEST(test_pop_on_empty_queue_returns_false);
    RUN_TEST(test_push_then_pop_returns_record);
    RUN_TEST(test_record_is_copied_into_queue);
    RUN_TEST(test_popped_record_is_a_copy);
    RUN_TEST(test_records_pop_in_order);
    RUN_TEST(test_push_on_full_queue_returns_false);
    RUN_TEST(test_queue_wraps_and_keeps_order);

    UNITY_END();
}

void loop() {}

int main(int argc, char **argv) {
    setup();
    return 0;
}
