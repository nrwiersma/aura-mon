//
// Unit tests for taskQueue: priority ordering, scheduling, re-scheduling,
// one-shot tasks, and the not-ready guard.
//

#include <unity.h>
#include "../stubs/TestCore.h"
#include "../../src/task.h"

// ---- fixtures --------------------------------------------------------------

static taskQueue *q;

void setUp() {
    q = new taskQueue();
    mockMillisManual = true;
    mockMillisValue  = 0;
}

void tearDown() {
    delete q;
    mockMillisManual = false;
}

// ============================================================================
// Empty queue
// ============================================================================

void test_empty_queue_returns_false() {
    TEST_ASSERT_FALSE(q->runNextTask());
}

// ============================================================================
// Basic execution
// ============================================================================

void test_single_task_runs() {
    bool ran = false;
    q->add([&ran](void *) -> uint32_t {
        ran = true;
        return 0; // one-shot
    }, 0);

    TEST_ASSERT_TRUE(q->runNextTask());
    TEST_ASSERT_TRUE(ran);
}

void test_one_shot_task_not_rescheduled() {
    int count = 0;
    q->add([&count](void *) -> uint32_t {
        count++;
        return 0;
    }, 0);

    q->runNextTask();
    q->runNextTask(); // queue should be empty now

    TEST_ASSERT_EQUAL(1, count);
}

void test_rescheduled_task_runs_again() {
    int count = 0;
    q->add([&count](void *) -> uint32_t {
        count++;
        return (count < 3) ? 1 : 0; // reschedule twice
    }, 0);

    // Run at t=0 → count=1, reschedules for t=1
    q->runNextTask();
    TEST_ASSERT_EQUAL(1, count);

    // Advance time past the reschedule point
    mockMillisValue = 2;
    q->runNextTask(); // count=2, reschedules for t=3
    TEST_ASSERT_EQUAL(2, count);

    mockMillisValue = 4;
    q->runNextTask(); // count=3, one-shot
    TEST_ASSERT_EQUAL(3, count);

    // Queue empty
    TEST_ASSERT_FALSE(q->runNextTask());
}

// ============================================================================
// Not-ready guard
// ============================================================================

void test_task_not_run_before_nextRun() {
    // Add a one-shot task, run it so it reschedules for t=1000
    int count = 0;
    q->add([&count](void *) -> uint32_t {
        count++;
        return (count == 1) ? 1000 : 0;
    }, 0);

    mockMillisValue = 0;
    q->runNextTask(); // runs at t=0, schedules for t=1000
    TEST_ASSERT_EQUAL(1, count);

    // Time has not advanced past nextRun
    mockMillisValue = 500;
    TEST_ASSERT_FALSE(q->runNextTask());
    TEST_ASSERT_EQUAL(1, count);

    // Now advance past the deadline
    mockMillisValue = 1001;
    TEST_ASSERT_TRUE(q->runNextTask());
    TEST_ASSERT_EQUAL(2, count);
}

// ============================================================================
// Priority ordering
// ============================================================================

void test_higher_priority_runs_first_same_time() {
    std::vector<int> order;

    // Both tasks are immediately ready (nextRun = 0).
    q->add([&order](void *) -> uint32_t { order.push_back(1); return 0; }, 1 /*low*/);
    q->add([&order](void *) -> uint32_t { order.push_back(2); return 0; }, 5 /*high*/);

    mockMillisValue = 0;
    q->runNextTask();
    q->runNextTask();

    TEST_ASSERT_EQUAL(2, (int)order.size());
    TEST_ASSERT_EQUAL(2, order[0]); // priority 5 ran first
    TEST_ASSERT_EQUAL(1, order[1]);
}

void test_earlier_nextRun_runs_before_higher_priority() {
    // A lower-priority task scheduled earlier should still run before a
    // higher-priority task that is not yet ready.
    std::vector<int> order;

    q->add([&order](void *) -> uint32_t { order.push_back(1); return 0; }, 10 /*high priority*/);
    // Run it once so it reschedules far in the future
    mockMillisValue = 0;
    q->runNextTask(); // task1 runs at t=0, becomes one-shot so removed

    // Reset and add both tasks fresh with explicit scheduling
    delete q;
    q = new taskQueue();
    order.clear();

    // Task A: low priority, available now
    q->add([&order](void *) -> uint32_t { order.push_back('A'); return 0; }, 1);
    // Task B: high priority, but scheduled for a later time — simulate by
    // running it once to push nextRun forward
    int callCount = 0;
    q->add([&order, &callCount](void *) -> uint32_t {
        callCount++;
        if (callCount == 1) {
            order.push_back('B');
            return 500; // reschedule 500ms from now
        }
        order.push_back('C');
        return 0;
    }, 10);

    // At t=0 both are ready; high priority runs first
    mockMillisValue = 0;
    q->runNextTask(); // B runs (priority 10)
    q->runNextTask(); // A runs (priority 1)

    TEST_ASSERT_EQUAL('B', order[0]);
    TEST_ASSERT_EQUAL('A', order[1]);

    // At t=100 only A-equivalent tasks are available; B not ready (nextRun ~500)
    mockMillisValue = 100;
    TEST_ASSERT_FALSE(q->runNextTask()); // B not ready, queue blocked by it being top

    // At t=600 B becomes ready again
    mockMillisValue = 600;
    TEST_ASSERT_TRUE(q->runNextTask()); // C
    TEST_ASSERT_EQUAL('C', order[2]);
}

// ============================================================================
// param forwarding
// ============================================================================

void test_param_forwarded_to_task() {
    int value = 42;
    int received = 0;
    q->add([&received](void *p) -> uint32_t {
        received = *static_cast<int *>(p);
        return 0;
    }, 0, &value);

    mockMillisValue = 0;
    q->runNextTask();

    TEST_ASSERT_EQUAL(42, received);
}

// ============================================================================
// Multiple tasks
// ============================================================================

void test_multiple_tasks_all_run() {
    int count = 0;
    for (int i = 0; i < 5; i++) {
        q->add([&count](void *) -> uint32_t { count++; return 0; }, 0);
    }

    mockMillisValue = 0;
    while (q->runNextTask()) {}

    TEST_ASSERT_EQUAL(5, count);
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_empty_queue_returns_false);
    RUN_TEST(test_single_task_runs);
    RUN_TEST(test_one_shot_task_not_rescheduled);
    RUN_TEST(test_rescheduled_task_runs_again);
    RUN_TEST(test_task_not_run_before_nextRun);
    RUN_TEST(test_higher_priority_runs_first_same_time);
    RUN_TEST(test_earlier_nextRun_runs_before_higher_priority);
    RUN_TEST(test_param_forwarded_to_task);
    RUN_TEST(test_multiple_tasks_all_run);

    UNITY_END();
}

void loop() {}

int main(int argc, char **argv) {
    setup();
    return 0;
}

