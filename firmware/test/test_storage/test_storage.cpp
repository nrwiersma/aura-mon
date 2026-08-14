//
// Unit tests for the storage transaction queue.
//
// submit() short-circuits to inline execution under UNIT_TEST, so these tests
// drive detail::pushJob and drain() directly to exercise the ring buffer.
//

#include <unity.h>
#include "../stubs/TestCore.h"
#include "../../src/storage.h"

namespace {
    int      runCount;
    uint32_t lastValue;
    size_t   lastPayload;

    // A trivially copyable closure stand-in with a controllable footprint.
    template<size_t N>
    struct payload {
        uint32_t value;
        uint8_t  filler[N];

        error operator()(storage::sdAccess &) const {
            runCount++;
            lastValue = value;
            lastPayload = N;
            return {};
        }
    };

    template<size_t N>
    bool push(const uint32_t value) {
        payload<N> p{value, {}};
        // Mirrors what submit() does on target.
        const storage::detail::invokeFn thunk = [](void *c, storage::sdAccess &sd) -> error {
            return (*static_cast<payload<N> *>(c))(sd);
        };
        return storage::detail::pushJob(thunk, &p, sizeof(p));
    }

    uint32_t depth() { return metrics.sd_queue_depth.load(); }
}

void setUp() {
    storage::init();
    storage::drain();

    sd.fileExists = false;
    if (sd.file) {
        delete sd.file;
        sd.file = nullptr;
    }
    sd.directories.clear();
    FsFile::failWrites = false;

    runCount = 0;
    lastValue = 0;
    lastPayload = 0;
    metrics.sd_queue_depth.store(0);
    metrics.sd_queue_high_water.store(0);
    metrics.sd_job_errors_total.store(0);
}

void tearDown() {
    storage::drain();
}

// ========== Basic Functionality Tests ==========

void test_storage_push_and_drain_one() {
    TEST_ASSERT_TRUE(push<8>(42));
    TEST_ASSERT_EQUAL(1, depth());

    storage::drain();

    TEST_ASSERT_EQUAL(1, runCount);
    TEST_ASSERT_EQUAL(42, lastValue);
    TEST_ASSERT_EQUAL(0, depth());
}

void test_storage_drain_empty_is_safe() {
    storage::drain();
    TEST_ASSERT_EQUAL(0, runCount);
}

void test_storage_preserves_fifo_order() {
    for (uint32_t i = 1; i <= 5; i++) {
        TEST_ASSERT_TRUE(push<8>(i));
    }

    // Pump runs exactly one job at a time, so ordering is observable.
    for (uint32_t i = 1; i <= 5; i++) {
        storage::pump();
        TEST_ASSERT_EQUAL(i, lastValue);
    }
    TEST_ASSERT_EQUAL(5, runCount);
}

void test_storage_pump_runs_single_job() {
    TEST_ASSERT_TRUE(push<8>(1));
    TEST_ASSERT_TRUE(push<8>(2));

    storage::pump();

    TEST_ASSERT_EQUAL(1, runCount);
    TEST_ASSERT_EQUAL(1, depth());
}

// ========== Ring Buffer Mechanics ==========

// Repeatedly filling and draining forces the ring to wrap many times, including
// entries that cannot fit in the space left before the end of the buffer.
void test_storage_wraps_around() {
    constexpr size_t big = 320; // Close to a LogRecord.

    uint32_t expected = 1;
    for (int round = 0; round < 40; round++) {
        int pushed = 0;
        for (int i = 0; i < 4; i++) {
            if (!push<big>(expected + static_cast<uint32_t>(pushed))) break;
            pushed++;
        }
        TEST_ASSERT_GREATER_THAN(0, pushed);

        for (int i = 0; i < pushed; i++) {
            storage::pump();
            TEST_ASSERT_EQUAL(expected + static_cast<uint32_t>(i), lastValue);
            TEST_ASSERT_EQUAL(big, lastPayload);
        }
        expected += static_cast<uint32_t>(pushed);
    }

    TEST_ASSERT_EQUAL(0, depth());
}

// Mixed sizes exercise the skip-marker path, where a large entry cannot fit in
// the gap left before the end of the ring.
void test_storage_mixed_sizes_wrap() {
    uint32_t next = 1;  // Next value to push.
    uint32_t done = 1;  // Next value expected out.

    for (int round = 0; round < 60; round++) {
        if (push<24>(next)) next++;
        if (push<400>(next)) next++;
        if (push<56>(next)) next++;

        // Leave one behind each round so the ring stays partially occupied and
        // the head and tail keep crossing the wrap point at different offsets.
        while (depth() > 1) {
            storage::pump();
            TEST_ASSERT_EQUAL(done, lastValue);
            done++;
        }
    }

    while (depth() > 0) {
        storage::pump();
        TEST_ASSERT_EQUAL(done, lastValue);
        done++;
    }

    TEST_ASSERT_EQUAL(next, done);
}

void test_storage_rejects_when_full() {
    int pushed = 0;
    while (push<400>(static_cast<uint32_t>(pushed + 1))) {
        pushed++;
        TEST_ASSERT_LESS_THAN(64, pushed); // Guard against a runaway loop.
    }

    // The queue must fill and refuse, never overwrite.
    TEST_ASSERT_GREATER_THAN(0, pushed);
    TEST_ASSERT_EQUAL(pushed, depth());

    storage::drain();

    TEST_ASSERT_EQUAL(pushed, runCount);
    TEST_ASSERT_EQUAL(0, depth());
}

void test_storage_rejects_oversized_job() {
    // Larger than jobMax, so it can never be queued.
    TEST_ASSERT_FALSE(push<storage::jobMax>(1));
    TEST_ASSERT_EQUAL(0, depth());
}

// ========== Primitives ==========
//
// Unity assertions longjmp out of the enclosing scope, so they must never run
// inside a transaction: the card lock would be left held. Results are captured
// and asserted after run() returns.

void test_storage_append_then_read() {
    constexpr char body[] = "hello world";
    char           buf[6] = {};

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.append("aura-mon/x.txt", body, sizeof(body) - 1); e) {
            return e;
        }
        return sd.readAt("aura-mon/x.txt", 6, buf, 5);
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL_STRING("world", buf);
}

void test_storage_write_at_offset() {
    char buf[4] = {};

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.append("f", "aaaa", 4); e) {
            return e;
        }
        if (auto e = sd.writeAt("f", 1, "bb", 2); e) {
            return e;
        }
        return sd.readAt("f", 0, buf, 4);
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL_MEMORY("abba", buf, 4);
}

// A caller asking for more than the file holds must be told, not handed a
// partly filled buffer it believes is complete.
void test_storage_short_read_is_an_error() {
    char buf[8] = {};

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.append("f", "ab", 2); e) {
            return e;
        }
        return sd.readAt("f", 0, buf, 8);
    });

    TEST_ASSERT_TRUE(err);
    TEST_ASSERT_EQUAL_STRING("short read", err.Error());
}

void test_storage_failed_write_is_an_error() {
    auto err = storage::run([](storage::sdAccess &sd) {
        FsFile::failWrites = true;
        return sd.writeAt("f", 0, "ab", 2);
    });

    TEST_ASSERT_TRUE(err);
    TEST_ASSERT_EQUAL_STRING("short write", err.Error());
}

void test_storage_mkdir_for_creates_parent() {
    auto err = storage::run([](storage::sdAccess &sd) {
        return sd.mkdirFor("aura-mon/nested/file.txt");
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL(1, sd.directories.size());
    TEST_ASSERT_EQUAL_STRING("aura-mon/nested", sd.directories[0].c_str());
}

void test_storage_mkdir_for_ignores_bare_name() {
    auto err = storage::run([](storage::sdAccess &sd) {
        return sd.mkdirFor("file.txt");
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL(0, sd.directories.size());
}

void test_storage_size_is_zero_when_missing() {
    bool     present = true;
    uint32_t size = 1;

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        present = sd.exists("f");
        size = sd.size("f");
        return {};
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL(0, size);
}

// A pinned handle survives between transactions, but must not survive the file
// being removed, or it would go on referring to freed clusters.
void test_storage_remove_drops_pinned_handle() {
    char buf[4] = {};
    bool stillThere = true;

    auto err = storage::run([](storage::sdAccess &sd) -> error {
        if (auto e = sd.append("f", "abcd", 4); e) {
            return e;
        }
        sd.pin("f");
        return {};
    });
    TEST_ASSERT_FALSE(err);

    err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.readAt("f", 0, buf, 4); e) {
            return e;
        }
        if (auto e = sd.remove("f"); e) {
            return e;
        }
        stillThere = sd.exists("f");
        return {};
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL_MEMORY("abcd", buf, 4);
    TEST_ASSERT_FALSE(stillThere);
}

void test_storage_read_json() {
    constexpr char body[] = R"({"format":1,"name":"aura"})";

    JsonDocument doc;
    auto         err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.append("c.json", body, sizeof(body) - 1); e) {
            return e;
        }
        return sd.readJSON("c.json", doc);
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL(1, doc["format"].as<int>());
    TEST_ASSERT_EQUAL_STRING("aura", doc["name"].as<const char *>());
}

void test_storage_read_json_rejects_garbage() {
    JsonDocument doc;
    auto         err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.append("c.json", "not json", 8); e) {
            return e;
        }
        return sd.readJSON("c.json", doc);
    });

    TEST_ASSERT_TRUE(err);
    TEST_ASSERT_EQUAL_STRING("could not decode file", err.Error());
}

// ========== Metrics ==========

void test_storage_high_water_tracks_peak() {
    for (uint32_t i = 1; i <= 3; i++) {
        TEST_ASSERT_TRUE(push<8>(i));
    }
    TEST_ASSERT_EQUAL(3, metrics.sd_queue_high_water.load());

    storage::drain();

    // High water is a peak, not a gauge; it must not fall back.
    TEST_ASSERT_EQUAL(3, metrics.sd_queue_high_water.load());
    TEST_ASSERT_EQUAL(0, depth());
}

void test_storage_counts_job_errors() {
    struct failing {
        error operator()(storage::sdAccess &) const { return newError("boom"); }
    };

    failing                         f{};
    const storage::detail::invokeFn thunk = [](void *c, storage::sdAccess &sd) -> error {
        return (*static_cast<failing *>(c))(sd);
    };
    TEST_ASSERT_TRUE(storage::detail::pushJob(thunk, &f, sizeof(f)));

    storage::drain();

    TEST_ASSERT_EQUAL(1, metrics.sd_job_errors_total.load());
}

// ========== Re-entrancy ==========

// A job may queue more work, which is what lets error paths log. The queue lock
// must not be held while a job runs, or this deadlocks.
void test_storage_job_can_submit() {
    struct nested {
        error operator()(storage::sdAccess &) const {
            runCount++;
            if (runCount == 1) {
                push<8>(99);
            }
            return {};
        }
    };

    nested                          n{};
    const storage::detail::invokeFn thunk = [](void *c, storage::sdAccess &sd) -> error {
        return (*static_cast<nested *>(c))(sd);
    };
    TEST_ASSERT_TRUE(storage::detail::pushJob(thunk, &n, sizeof(n)));

    storage::drain();

    TEST_ASSERT_EQUAL(2, runCount);
    TEST_ASSERT_EQUAL(99, lastValue);
}

// run() inside a transaction would deadlock on the non-recursive card lock, so
// it must fail instead.
void test_storage_nested_run_fails() {
    bool inner = false;

    auto err = storage::run([&](storage::sdAccess &) {
        TEST_ASSERT_TRUE(storage::inTransaction());

        auto nestedErr = storage::run([&](storage::sdAccess &) {
            inner = true;
            return error{};
        });
        TEST_ASSERT_TRUE(nestedErr);
        return error{};
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_FALSE(inner);
    TEST_ASSERT_FALSE(storage::inTransaction());
}

void test_storage_pump_noop_in_transaction() {
    TEST_ASSERT_TRUE(push<8>(7));

    auto err = storage::run([&](storage::sdAccess &) {
        storage::pump();
        return error{};
    });

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL(0, runCount);
    TEST_ASSERT_EQUAL(1, depth());
}

void test_storage_drainTask_never_returns_zero() {
    // Returning 0 removes a task from the queue permanently (task.cpp:31).
    TEST_ASSERT_GREATER_THAN(0, storage::drainTask(nullptr));

    for (uint32_t i = 1; i <= 8; i++) {
        push<8>(i);
    }
    TEST_ASSERT_GREATER_THAN(0, storage::drainTask(nullptr));
}

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_storage_push_and_drain_one);
    RUN_TEST(test_storage_drain_empty_is_safe);
    RUN_TEST(test_storage_preserves_fifo_order);
    RUN_TEST(test_storage_pump_runs_single_job);

    RUN_TEST(test_storage_wraps_around);
    RUN_TEST(test_storage_mixed_sizes_wrap);
    RUN_TEST(test_storage_rejects_when_full);
    RUN_TEST(test_storage_rejects_oversized_job);

    RUN_TEST(test_storage_append_then_read);
    RUN_TEST(test_storage_write_at_offset);
    RUN_TEST(test_storage_short_read_is_an_error);
    RUN_TEST(test_storage_failed_write_is_an_error);
    RUN_TEST(test_storage_mkdir_for_creates_parent);
    RUN_TEST(test_storage_mkdir_for_ignores_bare_name);
    RUN_TEST(test_storage_size_is_zero_when_missing);
    RUN_TEST(test_storage_remove_drops_pinned_handle);
    RUN_TEST(test_storage_read_json);
    RUN_TEST(test_storage_read_json_rejects_garbage);

    RUN_TEST(test_storage_high_water_tracks_peak);
    RUN_TEST(test_storage_counts_job_errors);
    RUN_TEST(test_storage_job_can_submit);

    RUN_TEST(test_storage_nested_run_fails);
    RUN_TEST(test_storage_pump_noop_in_transaction);
    RUN_TEST(test_storage_drainTask_never_returns_zero);

    UNITY_END();
}

void loop() {
    // Nothing to do here
}

int main(int argc, char **argv) {
    setup();
    return 0;
}
