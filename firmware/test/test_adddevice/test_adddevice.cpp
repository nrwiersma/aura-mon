//
// Unit tests for adddevice.cpp:
//   - findAvailableAddressLocked(): address allocation logic.
//   - addDeviceFromButton(): device creation, naming, flag setting, and action.
//

#include <unity.h>
#include "../stubs/TestCore.h"

// Forward declarations of functions under test (defined in adddevice.cpp).
uint8_t  findAvailableAddressLocked();
uint32_t addDeviceFromButton(void *param);

// ---- helpers ---------------------------------------------------------------

static void cleanupInfos() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        delete registry.infos[i];
        registry.infos[i] = nullptr;
    }
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    cleanupInfos();
    registry.changed       = false;
    registry.actionControl = {deviceActionType::None, 0};
}

void tearDown() {
    cleanupInfos();
}

// ============================================================================
// findAvailableAddressLocked
// ============================================================================

void test_find_address_all_empty_returns_one() {
    TEST_ASSERT_EQUAL(1, findAvailableAddressLocked());
}

void test_find_address_first_slot_occupied_returns_two() {
    registry.infos[0] = new inputDeviceInfo(1);

    TEST_ASSERT_EQUAL(2, findAvailableAddressLocked());
}

void test_find_address_gap_in_middle_returns_lowest_free() {
    registry.infos[0] = new inputDeviceInfo(1);
    registry.infos[2] = new inputDeviceInfo(3);

    TEST_ASSERT_EQUAL(2, findAvailableAddressLocked());
}

void test_find_address_all_full_returns_zero() {
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        registry.infos[i] = new inputDeviceInfo(i + 1);
    }
    TEST_ASSERT_EQUAL(0, findAvailableAddressLocked());
}

void test_find_address_skips_already_used_addr() {
    registry.infos[0] = new inputDeviceInfo(1);
    registry.infos[1] = new inputDeviceInfo(2);

    TEST_ASSERT_EQUAL(3, findAvailableAddressLocked());
}

// ============================================================================
// addDeviceFromButton
// ============================================================================

void test_add_device_creates_info_at_first_slot() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_NOT_NULL(registry.infos[0]);
    TEST_ASSERT_EQUAL(1, registry.infos[0]->addr);
    TEST_ASSERT_TRUE(registry.infos[0]->enabled);
}

void test_add_device_sets_default_name() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL_STRING("Device 1", registry.infos[0]->name.c_str());
}

void test_add_device_sets_devices_changed_flag() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_TRUE(registry.changed);
}

void test_add_device_sets_assign_action() {
    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL((uint8_t)deviceActionType::Assign,
                      (uint8_t)registry.actionControl.type);
    TEST_ASSERT_EQUAL(1, registry.actionControl.address);
}

void test_add_device_uses_next_free_slot() {
    registry.infos[0] = new inputDeviceInfo(1);

    addDeviceFromButton(nullptr);

    TEST_ASSERT_NOT_NULL(registry.infos[1]);
    TEST_ASSERT_EQUAL(2, registry.infos[1]->addr);
    TEST_ASSERT_EQUAL_STRING("Device 2", registry.infos[1]->name.c_str());
}

void test_add_device_returns_zero_when_full() {
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        registry.infos[i] = new inputDeviceInfo(i + 1);
    }

    uint32_t result = addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_FALSE(registry.changed);
}

void test_add_device_action_address_matches_created_device() {
    registry.infos[0] = new inputDeviceInfo(1);

    addDeviceFromButton(nullptr);

    TEST_ASSERT_EQUAL(2, registry.actionControl.address);
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_find_address_all_empty_returns_one);
    RUN_TEST(test_find_address_first_slot_occupied_returns_two);
    RUN_TEST(test_find_address_gap_in_middle_returns_lowest_free);
    RUN_TEST(test_find_address_all_full_returns_zero);
    RUN_TEST(test_find_address_skips_already_used_addr);

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

