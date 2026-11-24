//
// Unit tests for add_device.cpp:
//   - findAvailableAddress(): address allocation logic.
//   - addDeviceFromButton(): device creation, naming and action.
//

#include <unity.h>
#include <cstring>
#include "../stubs/TestCore.h"

uint8_t  findAvailableAddress();
uint32_t addDeviceFromButton(void *param);

static void cleanupDevices() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i]) {
            delete devices[i];
            devices[i] = nullptr;
        }
    }
}

void setUp() {
    cleanupDevices();
    deviceAction = {DeviceActionType::None, 0};
}

void tearDown() {
    cleanupDevices();
}

void test_find_address_all_empty_returns_one() {
    TEST_ASSERT_EQUAL(1, findAvailableAddress());
}

void test_find_address_first_slot_occupied_returns_two() {
    devices[0] = new InputDevice(1);

    TEST_ASSERT_EQUAL(2, findAvailableAddress());
}

void test_find_address_gap_in_middle_returns_lowest_free() {
    devices[0] = new InputDevice(1);
    devices[2] = new InputDevice(3);

    TEST_ASSERT_EQUAL(2, findAvailableAddress());
}

void test_find_address_all_full_returns_zero() {
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        devices[i] = new InputDevice(i + 1);
    }

    TEST_ASSERT_EQUAL(0, findAvailableAddress());
}

void test_find_address_skips_already_used_addr() {
    devices[0] = new InputDevice(1);
    devices[1] = new InputDevice(2);

    TEST_ASSERT_EQUAL(3, findAvailableAddress());
}

void test_add_device_creates_device_at_first_slot() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_NOT_NULL(devices[0]);
    TEST_ASSERT_EQUAL(1, devices[0]->addr);
    TEST_ASSERT_TRUE(devices[0]->enabled);
}

void test_add_device_sets_default_name() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL_STRING("Device 1", devices[0]->name.c_str());
}

void test_add_device_sets_assign_action() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL((uint8_t) DeviceActionType::Assign, (uint8_t) deviceAction.type);
    TEST_ASSERT_EQUAL(1, deviceAction.address);
}

void test_add_device_uses_next_free_slot() {
    devices[0] = new InputDevice(1);

    addDeviceFromButton(nullptr);

    TEST_ASSERT_NOT_NULL(devices[1]);
    TEST_ASSERT_EQUAL(2, devices[1]->addr);
    TEST_ASSERT_EQUAL_STRING("Device 2", devices[1]->name.c_str());
}

void test_add_device_returns_zero_when_full() {
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        devices[i] = new InputDevice(i + 1);
    }

    uint32_t result = addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL((uint8_t) DeviceActionType::None, (uint8_t) deviceAction.type);
}

void test_add_device_action_address_matches_created_device() {
    devices[0] = new InputDevice(1);

    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL(2, deviceAction.address);
}

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_find_address_all_empty_returns_one);
    RUN_TEST(test_find_address_first_slot_occupied_returns_two);
    RUN_TEST(test_find_address_gap_in_middle_returns_lowest_free);
    RUN_TEST(test_find_address_all_full_returns_zero);
    RUN_TEST(test_find_address_skips_already_used_addr);

    RUN_TEST(test_add_device_creates_device_at_first_slot);
    RUN_TEST(test_add_device_sets_default_name);
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
