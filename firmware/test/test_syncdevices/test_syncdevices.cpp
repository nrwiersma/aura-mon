//
// Unit tests for DeviceRegistry sync methods and the syncDevices task.
//

#include <unity.h>
#include "../stubs/TestCore.h"

// syncDevices is still a free function in syncdevices.cpp.
uint32_t syncDevices(void *param);

// ---- helpers ---------------------------------------------------------------

static void cleanupRegistry() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        delete registry.infos[i];
        registry.infos[i] = nullptr;
        delete registry.devices[i];
        registry.devices[i] = nullptr;
        delete registry.data[i];
        registry.data[i] = nullptr;
    }
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    cleanupRegistry();
    registry.changed = false;
    registry.actionControl = {DeviceActionType::None, 0};
    registry.actionData = {DeviceActionType::None, 0};
}

void tearDown() {
    cleanupRegistry();
}

// ============================================================================
// syncInfo – creation
// ============================================================================

void test_sync_info_creates_device_for_new_info() {
    auto *info = new InputDeviceInfo(1);
    info->enabled = true;
    info->name = "Sensor A";
    registry.infos[0] = info;

    registry.syncInfo();

    TEST_ASSERT_NOT_NULL(registry.devices[0]);
    TEST_ASSERT_EQUAL(1, registry.devices[0]->addr);
    TEST_ASSERT_TRUE(registry.devices[0]->enabled);
    TEST_ASSERT_EQUAL_STRING("Sensor A", registry.devices[0]->name.c_str());
}

void test_sync_info_copies_calibration_and_reversed() {
    auto *info = new InputDeviceInfo(2);
    info->enabled = true;
    info->calibration = 1.2f;
    info->reversed = true;
    registry.infos[1] = info;

    registry.syncInfo();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2f, registry.devices[1]->calibration);
    TEST_ASSERT_TRUE(registry.devices[1]->reversed);
}

// ============================================================================
// syncInfo – update
// ============================================================================

void test_sync_info_updates_existing_device() {
    auto *info = new InputDeviceInfo(1);
    info->enabled = true;
    info->name = "Old";
    registry.infos[0] = info;
    registry.syncInfo();

    registry.infos[0]->name = "New";
    registry.infos[0]->enabled = false;

    registry.syncInfo();

    TEST_ASSERT_EQUAL_STRING("New", registry.devices[0]->name.c_str());
    TEST_ASSERT_FALSE(registry.devices[0]->enabled);
}

// ============================================================================
// syncInfo – removal
// ============================================================================

void test_sync_info_removes_device_when_info_null() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->name = "Ghost";
    // registry.infos[0] remains nullptr.

    registry.syncInfo();

    TEST_ASSERT_NULL(registry.devices[0]);
}

void test_sync_info_empty_info_name_gives_empty_device_name() {
    auto *info = new InputDeviceInfo(3);
    info->enabled = true;
    info->name = "";
    registry.infos[2] = info;

    registry.syncInfo();

    TEST_ASSERT_TRUE(registry.devices[2]->name.isEmpty());
}

// ============================================================================
// syncAction
// ============================================================================

void test_sync_action_forwards_control_to_data() {
    registry.actionControl = {DeviceActionType::Locate, 5};

    registry.syncAction();

    TEST_ASSERT_EQUAL((uint8_t) DeviceActionType::Locate, (uint8_t) registry.actionData.type);
    TEST_ASSERT_EQUAL(5, registry.actionData.address);
}

void test_sync_action_clears_control_after_forward() {
    registry.actionControl = {DeviceActionType::Assign, 3};

    registry.syncAction();

    TEST_ASSERT_EQUAL((uint8_t) DeviceActionType::None, (uint8_t) registry.actionControl.type);
    TEST_ASSERT_EQUAL(0, registry.actionControl.address);
}

void test_sync_action_no_op_when_none() {
    registry.actionControl = {DeviceActionType::None, 0};
    registry.actionData = {DeviceActionType::Assign, 7};

    registry.syncAction();

    TEST_ASSERT_EQUAL((uint8_t) DeviceActionType::Assign, (uint8_t) registry.actionData.type);
    TEST_ASSERT_EQUAL(7, registry.actionData.address);
}

// ============================================================================
// syncData – amps / pf / hz derivation
// ============================================================================

void test_sync_data_creates_data_entry_for_active_device() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->current.volts = 230.0;
    registry.devices[0]->current.va = 1150.0;
    registry.devices[0]->current.watts = 1035.0;
    registry.devices[0]->current.hz = 50.0;
    registry.devices[0]->name = "Dev1";

    registry.syncData();

    TEST_ASSERT_NOT_NULL(registry.data[0]);
    TEST_ASSERT_EQUAL_STRING("Dev1", registry.data[0]->name.c_str());
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0, registry.data[0]->volts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 50.0, registry.data[0]->hz);
}

void test_sync_data_amps_derived_from_va_over_volts() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->current.volts = 230.0;
    registry.devices[0]->current.va = 1150.0;

    registry.syncData();

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 5.0, registry.data[0]->amps);
}

void test_sync_data_amps_zero_when_voltage_zero() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->current.volts = 0.0;
    registry.devices[0]->current.va = 1150.0;

    registry.syncData();

    TEST_ASSERT_EQUAL_DOUBLE(0.0, registry.data[0]->amps);
}

void test_sync_data_pf_derived_from_watts_over_va() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->current.volts = 230.0;
    registry.devices[0]->current.va = 1000.0;
    registry.devices[0]->current.watts = 900.0;

    registry.syncData();

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.9, registry.data[0]->pf);
}

void test_sync_data_pf_zero_when_va_zero() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->current.volts = 230.0;
    registry.devices[0]->current.va = 0.0;
    registry.devices[0]->current.watts = 0.0;

    registry.syncData();

    TEST_ASSERT_EQUAL_DOUBLE(0.0, registry.data[0]->pf);
}

void test_sync_data_removes_data_when_device_null() {
    registry.data[0] = new InputDeviceData{};
    registry.data[0]->name = "Gone";
    // registry.devices[0] is nullptr.

    registry.syncData();

    TEST_ASSERT_NULL(registry.data[0]);
}

void test_sync_data_empty_device_name_gives_empty_data_name() {
    registry.devices[0] = new InputDevice(1);
    registry.devices[0]->current.volts = 230.0;
    registry.devices[0]->name = "";

    registry.syncData();

    TEST_ASSERT_TRUE(registry.data[0]->name.isEmpty());
}

// ============================================================================
// syncDevices – integration (devicesChanged flag)
// ============================================================================

void test_sync_devices_clears_changed_flag() {
    registry.changed = true;
    auto *info = new InputDeviceInfo(1);
    info->enabled = true;
    info->name = "X";
    registry.infos[0] = info;

    syncDevices(nullptr);

    TEST_ASSERT_FALSE(registry.changed);
}

void test_sync_devices_returns_1000() {
    TEST_ASSERT_EQUAL(1000, syncDevices(nullptr));
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_sync_info_creates_device_for_new_info);
    RUN_TEST(test_sync_info_copies_calibration_and_reversed);
    RUN_TEST(test_sync_info_updates_existing_device);
    RUN_TEST(test_sync_info_removes_device_when_info_null);
    RUN_TEST(test_sync_info_empty_info_name_gives_empty_device_name);

    RUN_TEST(test_sync_action_forwards_control_to_data);
    RUN_TEST(test_sync_action_clears_control_after_forward);
    RUN_TEST(test_sync_action_no_op_when_none);

    RUN_TEST(test_sync_data_creates_data_entry_for_active_device);
    RUN_TEST(test_sync_data_amps_derived_from_va_over_volts);
    RUN_TEST(test_sync_data_amps_zero_when_voltage_zero);
    RUN_TEST(test_sync_data_pf_derived_from_watts_over_va);
    RUN_TEST(test_sync_data_pf_zero_when_va_zero);
    RUN_TEST(test_sync_data_removes_data_when_device_null);
    RUN_TEST(test_sync_data_empty_device_name_gives_empty_data_name);

    RUN_TEST(test_sync_devices_clears_changed_flag);
    RUN_TEST(test_sync_devices_returns_1000);

    UNITY_END();
}

void loop() {
}

int main(int argc, char **argv) {
    setup();
    return 0;
}
