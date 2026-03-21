//
// Unit tests for InputDevice: accumulate(), setEnergy(), and reset().
//

#include <unity.h>
#include "../stubs/TestCore.h"
#include "../../src/device.h"

// Helper: reset millis stub to a known value before each test.
static void setMillis(unsigned long v) {
    mockMillisManual = true;
    mockMillisValue  = v;
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    rp2040.reset();
}

void tearDown() {
    mockMillisManual = false;
}

// ============================================================================
// Bucket default constructor
// ============================================================================

void test_bucket_defaults_to_zero_energy() {
    setMillis(1000);
    Bucket b;
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.volts);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.watts);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.va);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.hz);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.voltHrs);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.wattHrs);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.vaHrs);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b.hzHrs);
    TEST_ASSERT_EQUAL_UINT32(1000, b.ts);
}

// ============================================================================
// InputDevice::accumulate()
// ============================================================================

void test_accumulate_no_op_when_now_equals_ts() {
    setMillis(5000);
    InputDevice dev(1);
    dev.current.volts = 230.0;
    dev.current.watts = 1000.0;
    dev.current.va    = 1100.0;
    dev.current.hz    = 50.0;
    dev.current.ts    = 5000;

    dev.accumulate(5000);

    TEST_ASSERT_EQUAL_DOUBLE(0.0, dev.current.voltHrs);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, dev.current.wattHrs);
}

void test_accumulate_no_op_when_now_before_ts() {
    setMillis(5000);
    InputDevice dev(1);
    dev.current.volts = 230.0;
    dev.current.ts    = 5000;

    dev.accumulate(4000); // earlier than ts

    TEST_ASSERT_EQUAL_DOUBLE(0.0, dev.current.voltHrs);
}

void test_accumulate_integrates_one_hour() {
    InputDevice dev(1);
    dev.current.volts = 230.0;
    dev.current.watts = 1000.0;
    dev.current.va    = 1100.0;
    dev.current.hz    = 50.0;
    dev.current.ts    = 0;

    // 3,600,000 ms = 1 hour
    dev.accumulate(MS_PER_HOUR);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0,  dev.current.voltHrs);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1000.0, dev.current.wattHrs);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1100.0, dev.current.vaHrs);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 50.0,   dev.current.hzHrs);
    TEST_ASSERT_EQUAL_UINT32(MS_PER_HOUR, dev.current.ts);
}

void test_accumulate_half_hour() {
    InputDevice dev(1);
    dev.current.volts = 230.0;
    dev.current.watts = 2000.0;
    dev.current.ts    = 0;

    dev.accumulate(MS_PER_HOUR / 2);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 115.0,  dev.current.voltHrs);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1000.0, dev.current.wattHrs);
}

void test_accumulate_updates_timestamp() {
    InputDevice dev(1);
    dev.current.ts = 1000;

    dev.accumulate(2000);

    TEST_ASSERT_EQUAL_UINT32(2000, dev.current.ts);
}

void test_accumulate_called_twice_is_additive() {
    InputDevice dev(1);
    dev.current.volts = 100.0;
    dev.current.ts    = 0;

    dev.accumulate(MS_PER_HOUR);       // +100 Vhr
    dev.accumulate(MS_PER_HOUR * 2);   // another +100 Vhr

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 200.0, dev.current.voltHrs);
}

// ============================================================================
// InputDevice::setEnergy()
// ============================================================================

void test_setEnergy_stores_readings() {
    setMillis(0);
    InputDevice dev(1);
    dev.current.ts = 0;

    setMillis(MS_PER_HOUR); // advance time by 1 hour before the call
    dev.setEnergy(230.0, 1000.0, 1100.0, 50.0);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0,  dev.current.volts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1000.0, dev.current.watts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1100.0, dev.current.va);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 50.0,   dev.current.hz);
}

void test_setEnergy_calls_accumulate() {
    // ts starts at 0 (bucket constructor uses millis() which we set to 0 here)
    setMillis(0);
    InputDevice dev(1);
    // bucket ts is whatever millis() returned at construction; set it explicitly.
    dev.current.ts = 0;

    setMillis(MS_PER_HOUR);
    dev.setEnergy(230.0, 1000.0, 1100.0, 50.0);

    // accumulate should have been called with millis() == MS_PER_HOUR
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0, dev.current.voltHrs);
}

// ============================================================================
// InputDevice::reset()
// ============================================================================

void test_reset_clears_enabled() {
    InputDevice dev(1);
    dev.enabled = true;

    dev.reset();

    TEST_ASSERT_FALSE(dev.enabled);
}

void test_reset_frees_name() {
    InputDevice dev(1);
    dev.name = "My Device";

    dev.reset();

    TEST_ASSERT_TRUE(dev.name.isEmpty());
}

void test_reset_zeroes_calibration() {
    InputDevice dev(1);
    dev.calibration = 1.5f;

    dev.reset();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, dev.calibration);
}

void test_reset_clears_reversed() {
    InputDevice dev(1);
    dev.reversed = true;

    dev.reset();

    TEST_ASSERT_FALSE(dev.reversed);
}

// ============================================================================
// InputDeviceInfo helpers
// ============================================================================

void test_isEnabled_returns_enabled_state() {
    InputDeviceInfo info(1);
    TEST_ASSERT_FALSE(info.isEnabled());
    info.enabled = true;
    TEST_ASSERT_TRUE(info.isEnabled());
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_bucket_defaults_to_zero_energy);

    RUN_TEST(test_accumulate_no_op_when_now_equals_ts);
    RUN_TEST(test_accumulate_no_op_when_now_before_ts);
    RUN_TEST(test_accumulate_integrates_one_hour);
    RUN_TEST(test_accumulate_half_hour);
    RUN_TEST(test_accumulate_updates_timestamp);
    RUN_TEST(test_accumulate_called_twice_is_additive);

    RUN_TEST(test_setEnergy_stores_readings);
    RUN_TEST(test_setEnergy_calls_accumulate);

    RUN_TEST(test_reset_clears_enabled);
    RUN_TEST(test_reset_frees_name);
    RUN_TEST(test_reset_zeroes_calibration);
    RUN_TEST(test_reset_clears_reversed);

    RUN_TEST(test_isEnabled_returns_enabled_state);

    UNITY_END();
}

void loop() {}

int main(int argc, char **argv) {
    setup();
    return 0;
}

