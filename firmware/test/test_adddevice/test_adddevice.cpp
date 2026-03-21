//
// Unit tests for adddevice.cpp:
//   - findAvailableAddressLocked(): address allocation logic.
//   - addDeviceFromButton(): device creation, naming, flag setting, and action.
//

#include <unity.h>
#include <cstring>
#include "../stubs/TestCore.h"

// Forward declarations of functions under test (defined in adddevice.cpp).
uint8_t  findAvailableAddressLocked();
uint32_t addDeviceFromButton(void *param);

// ---- helpers ---------------------------------------------------------------

static void cleanupDeviceInfos() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            delete deviceInfos[i];
            deviceInfos[i] = nullptr;
        }
    }
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    cleanupDeviceInfos();
    devicesChanged = false;
    deviceActionControl = {DeviceActionType::None, 0};
}

void tearDown() {
    cleanupDeviceInfos();
}

// ============================================================================
// findAvailableAddressLocked
// ============================================================================

void test_find_address_all_empty_returns_one() {
    TEST_ASSERT_EQUAL(1, findAvailableAddressLocked());
}

void test_find_address_first_slot_occupied_returns_two() {
    deviceInfos[0] = new InputDeviceInfo(1);

    TEST_ASSERT_EQUAL(2, findAvailableAddressLocked());
}

void test_find_address_gap_in_middle_returns_lowest_free() {
    deviceInfos[0] = new InputDeviceInfo(1); // addr 1 used
    deviceInfos[2] = new InputDeviceInfo(3); // addr 3 used (slot 1 free)

    // Slot 1 (index 1) is free, addr would be 2.
    TEST_ASSERT_EQUAL(2, findAvailableAddressLocked());
}

void test_find_address_all_full_returns_zero() {
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        deviceInfos[i] = new InputDeviceInfo(i + 1);
    }
    TEST_ASSERT_EQUAL(0, findAvailableAddressLocked());
}

void test_find_address_skips_already_used_addr() {
    // Populate addresses 1 and 2; expect 3.
    deviceInfos[0] = new InputDeviceInfo(1);
    deviceInfos[1] = new InputDeviceInfo(2);

    TEST_ASSERT_EQUAL(3, findAvailableAddressLocked());
}

// ============================================================================
// addDeviceFromButton
// ============================================================================

void test_add_device_creates_info_at_first_slot() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_NOT_NULL(deviceInfos[0]);
    TEST_ASSERT_EQUAL(1, deviceInfos[0]->addr);
    TEST_ASSERT_TRUE(deviceInfos[0]->enabled);
}

void test_add_device_sets_default_name() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_FALSE(deviceInfos[0]->name.isEmpty());
    TEST_ASSERT_EQUAL_STRING("Device 1", deviceInfos[0]->name.c_str());
}

void test_add_device_sets_devices_changed_flag() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_TRUE(devicesChanged);
}

void test_add_device_sets_assign_action() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL((uint8_t)DeviceActionType::Assign,
                      (uint8_t)deviceActionControl.type);
    TEST_ASSERT_EQUAL(1, deviceActionControl.address);
}

void test_add_device_uses_next_free_slot() {
    // Pre-occupy slot 0.
    deviceInfos[0] = new InputDeviceInfo(1);

    addDeviceFromButton(nullptr);

    TEST_ASSERT_NOT_NULL(deviceInfos[1]);
    TEST_ASSERT_EQUAL(2, deviceInfos[1]->addr);
    TEST_ASSERT_EQUAL_STRING("Device 2", deviceInfos[1]->name.c_str());
}

void test_add_device_returns_zero_when_full() {
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        deviceInfos[i] = new InputDeviceInfo(i + 1);
    }

    uint32_t result = addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL(0, result);
    // devicesChanged must NOT have been set.
    TEST_ASSERT_FALSE(devicesChanged);
}

void test_add_device_action_address_matches_created_device() {
    // Pre-occupy slot 0 so the new device gets address 2.
    deviceInfos[0] = new InputDeviceInfo(1);

    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL(2, deviceActionControl.address);
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    // findAvailableAddressLocked
    RUN_TEST(test_find_address_all_empty_returns_one);
    RUN_TEST(test_find_address_first_slot_occupied_returns_two);
    RUN_TEST(test_find_address_gap_in_middle_returns_lowest_free);
    RUN_TEST(test_find_address_all_full_returns_zero);
    RUN_TEST(test_find_address_skips_already_used_addr);

    // addDeviceFromButton
    RUN_TEST(test_add_device_creates_info_at_first_slot);
    RUN_TEST(test_add_device_sets_default_name);
    RUN_TEST(test_add_device_sets_devices_changed_flag);
    RUN_TEST(test_add_device_sets_assign_action);
    RUN_TEST(test_add_device_uses_next_free_slot);
    RUN_TEST(test_add_device_returns_zero_when_full);
    RUN_TEST(test_add_device_action_address_matches_created_device);

    UNITY_END();
}

void loop() {}

int main(int argc, char **argv) {
    setup();
    return 0;
}

