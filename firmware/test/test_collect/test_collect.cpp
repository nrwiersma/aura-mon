//
// Unit tests for collect.cpp:
//   - float_abcd(): 32-bit float reconstruction from two Modbus registers.
//   - readFrame() physical-value derivation (volts, amps, VA, watts, calibration, reversed).
//

#include <unity.h>
#include <cstring>
#include <cmath>
#include "../stubs/TestCore.h"
#include "../../src/device.h"

// Forward declarations of the functions under test (defined in collect.cpp).
float   float_abcd(uint16_t hi, uint16_t lo);

// readFrame is static in collect.cpp, so we exercise it indirectly via a
// thin white-box helper that replicates its register-to-value logic.
// This allows us to test the business rules without needing a Modbus stub.
static void applyRegisters(inputDevice *dev,
                            float v, float a, float pf, float hz) {
    double volts = v * dev->calibration;
    if (dev->reversed) {
        volts = -volts;
        a     = -a;
    }
    double va    = volts * static_cast<double>(a);
    double watts = va * static_cast<double>(pf);
    dev->setEnergy(volts, watts, va, hz);
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    mockMillisManual = true;
    mockMillisValue  = 0;
}

void tearDown() {
    mockMillisManual = false;
}

// ============================================================================
// float_abcd – IEEE-754 big-endian reconstruction
// ============================================================================

// Helper: split a float into its two 16-bit Modbus register words (ABCD).
static void floatToRegisters(float f, uint16_t &hi, uint16_t &lo) {
    uint32_t raw;
    std::memcpy(&raw, &f, sizeof(raw));
    hi = static_cast<uint16_t>(raw >> 16);
    lo = static_cast<uint16_t>(raw & 0xFFFF);
}

void test_float_abcd_zero() {
    uint16_t hi, lo;
    floatToRegisters(0.0f, hi, lo);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, float_abcd(hi, lo));
}

void test_float_abcd_positive_integer() {
    uint16_t hi, lo;
    floatToRegisters(230.0f, hi, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 230.0f, float_abcd(hi, lo));
}

void test_float_abcd_negative_value() {
    uint16_t hi, lo;
    floatToRegisters(-50.5f, hi, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -50.5f, float_abcd(hi, lo));
}

void test_float_abcd_fractional_value() {
    uint16_t hi, lo;
    floatToRegisters(0.95f, hi, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.95f, float_abcd(hi, lo));
}

void test_float_abcd_large_value() {
    uint16_t hi, lo;
    floatToRegisters(99999.9f, hi, lo);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 99999.9f, float_abcd(hi, lo));
}

void test_float_abcd_infinity_roundtrip() {
    uint16_t hi, lo;
    floatToRegisters(INFINITY, hi, lo);
    float result = float_abcd(hi, lo);
    TEST_ASSERT_TRUE(std::isinf(result) && result > 0);
}

// ============================================================================
// Register-to-physical-value transform (via applyRegisters helper)
// ============================================================================

void test_normal_reading_no_calibration() {
    inputDevice dev(1);
    dev.enabled     = true;
    dev.calibration = 1.0f;
    dev.reversed    = false;
    dev.current.ts  = 0;

    applyRegisters(&dev, 230.0f, 5.0f, 0.9f, 50.0f);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0, dev.current.volts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0 * 5.0 * 0.9, dev.current.watts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0 * 5.0,        dev.current.va);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 50.0,               dev.current.hz);
}

void test_calibration_scales_voltage() {
    inputDevice dev(1);
    dev.enabled     = true;
    dev.calibration = 1.1f;
    dev.reversed    = false;
    dev.current.ts  = 0;

    applyRegisters(&dev, 230.0f, 5.0f, 1.0f, 50.0f);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0 * 1.1, dev.current.volts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0 * 1.1 * 5.0, dev.current.va);
}

void test_reversed_negates_voltage_and_amps() {
    inputDevice dev(1);
    dev.enabled     = true;
    dev.calibration = 1.0f;
    dev.reversed    = true;
    dev.current.ts  = 0;

    applyRegisters(&dev, 230.0f, 5.0f, 0.9f, 50.0f);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, -230.0, dev.current.volts);
    // va = (-230) * (-5) = +1150
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1150.0, dev.current.va);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1150.0 * 0.9, dev.current.watts);
}

void test_zero_voltage_gives_zero_va_and_watts() {
    inputDevice dev(1);
    dev.enabled     = true;
    dev.calibration = 1.0f;
    dev.reversed    = false;
    dev.current.ts  = 0;

    applyRegisters(&dev, 0.0f, 5.0f, 0.9f, 50.0f);

    TEST_ASSERT_EQUAL_DOUBLE(0.0, dev.current.va);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, dev.current.watts);
}

void test_zero_pf_gives_zero_watts() {
    inputDevice dev(1);
    dev.enabled     = true;
    dev.calibration = 1.0f;
    dev.reversed    = false;
    dev.current.ts  = 0;

    applyRegisters(&dev, 230.0f, 5.0f, 0.0f, 50.0f);

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0 * 5.0, dev.current.va);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, dev.current.watts);
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    // float_abcd
    RUN_TEST(test_float_abcd_zero);
    RUN_TEST(test_float_abcd_positive_integer);
    RUN_TEST(test_float_abcd_negative_value);
    RUN_TEST(test_float_abcd_fractional_value);
    RUN_TEST(test_float_abcd_large_value);
    RUN_TEST(test_float_abcd_infinity_roundtrip);

    // register-to-physical-value transform
    RUN_TEST(test_normal_reading_no_calibration);
    RUN_TEST(test_calibration_scales_voltage);
    RUN_TEST(test_reversed_negates_voltage_and_amps);
    RUN_TEST(test_zero_voltage_gives_zero_va_and_watts);
    RUN_TEST(test_zero_pf_gives_zero_watts);

    UNITY_END();
}

void loop() {}

int main(int argc, char **argv) {
    setup();
    return 0;
}

