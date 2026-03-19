//
// Unit tests for syncdevices.cpp:
//   - syncDeviceInfo(): device creation, update, and removal.
//   - syncDeviceAction(): action forwarding and clearing.
//   - syncDeviceData(): amps/pf derivation and zero-guard.
//

#include <unity.h>
#include <cstring>
#include "../stubs/TestCore.h"

// Forward declarations of functions under test.
void     syncDeviceInfo();
void     syncDeviceAction();
void     syncDeviceData();
uint32_t syncDevices(void *param);

// ---- helpers ---------------------------------------------------------------

static void cleanupDeviceInfos() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            delete deviceInfos[i];
            deviceInfos[i] = nullptr;
        }
    }
}

static void cleanupDevices() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i]) {
            delete devices[i];
            devices[i] = nullptr;
        }
    }
}

static void cleanupDeviceData() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (deviceData[i]) {
            delete deviceData[i];
            deviceData[i] = nullptr;
        }
    }
}

// ---- fixtures --------------------------------------------------------------

void setUp() {
    cleanupDeviceInfos();
    cleanupDevices();
    cleanupDeviceData();
    devicesChanged = false;
    deviceActionControl = {deviceActionType::None, 0};
    deviceActionData    = {deviceActionType::None, 0};
}

void tearDown() {
    cleanupDeviceInfos();
    cleanupDevices();
    cleanupDeviceData();
}

// ============================================================================
// syncDeviceInfo – creation
// ============================================================================

void test_sync_info_creates_device_for_new_info() {
    auto *info    = new inputDeviceInfo(1);
    info->enabled = true;
    info->name    = "Sensor A";
    deviceInfos[0] = info;

    syncDeviceInfo();

    TEST_ASSERT_NOT_NULL(devices[0]);
    TEST_ASSERT_EQUAL(1,      devices[0]->addr);
    TEST_ASSERT_TRUE(devices[0]->enabled);
    TEST_ASSERT_EQUAL_STRING("Sensor A", devices[0]->name.c_str());
}

void test_sync_info_copies_calibration_and_reversed() {
    auto *info         = new inputDeviceInfo(2);
    info->enabled      = true;
    info->calibration  = 1.2f;
    info->reversed     = true;
    deviceInfos[1]     = info;

    syncDeviceInfo();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.2f, devices[1]->calibration);
    TEST_ASSERT_TRUE(devices[1]->reversed);
}

// ============================================================================
// syncDeviceInfo – update
// ============================================================================

void test_sync_info_updates_existing_device() {
    // Create device first.
    auto *info    = new inputDeviceInfo(1);
    info->enabled = true;
    info->name    = "Old";
    deviceInfos[0] = info;
    syncDeviceInfo();

    // Update the info.
    deviceInfos[0]->name    = "New";
    deviceInfos[0]->enabled = false;

    syncDeviceInfo();

    TEST_ASSERT_EQUAL_STRING("New", devices[0]->name.c_str());
    TEST_ASSERT_FALSE(devices[0]->enabled);
}

// ============================================================================
// syncDeviceInfo – removal
// ============================================================================

void test_sync_info_removes_device_when_info_null() {
    // Create a device without info to simulate prior state.
    devices[0] = new inputDevice(1);
    devices[0]->name = "Ghost";
    // deviceInfos[0] remains nullptr.

    syncDeviceInfo();

    TEST_ASSERT_NULL(devices[0]);
}

void test_sync_info_null_info_name_gives_null_device_name() {
    auto *info  = new inputDeviceInfo(3);
    info->enabled = true;
    deviceInfos[2] = info;

    syncDeviceInfo();

    TEST_ASSERT_TRUE(devices[2]->name.isEmpty());
}

// ============================================================================
// syncDeviceAction
// ============================================================================

void test_sync_action_forwards_control_to_data() {
    deviceActionControl = {deviceActionType::Locate, 5};

    syncDeviceAction();

    TEST_ASSERT_EQUAL((uint8_t)deviceActionType::Locate, (uint8_t)deviceActionData.type);
    TEST_ASSERT_EQUAL(5, deviceActionData.address);
}

void test_sync_action_clears_control_after_forward() {
    deviceActionControl = {deviceActionType::Assign, 3};

    syncDeviceAction();

    TEST_ASSERT_EQUAL((uint8_t)deviceActionType::None, (uint8_t)deviceActionControl.type);
    TEST_ASSERT_EQUAL(0, deviceActionControl.address);
}

void test_sync_action_no_op_when_none() {
    deviceActionControl = {deviceActionType::None, 0};
    deviceActionData    = {deviceActionType::Assign, 7}; // pre-existing data

    syncDeviceAction();

    // data must be unchanged because control was None.
    TEST_ASSERT_EQUAL((uint8_t)deviceActionType::Assign, (uint8_t)deviceActionData.type);
    TEST_ASSERT_EQUAL(7, deviceActionData.address);
}

// ============================================================================
// syncDeviceData – amps / pf / hz derivation
// ============================================================================

void test_sync_data_creates_data_entry_for_active_device() {
    devices[0] = new inputDevice(1);
    devices[0]->current.volts = 230.0;
    devices[0]->current.va    = 1150.0;
    devices[0]->current.watts = 1035.0;
    devices[0]->current.hz    = 50.0;
    devices[0]->name          = "Dev1";

    syncDeviceData();

    TEST_ASSERT_NOT_NULL(deviceData[0]);
    TEST_ASSERT_EQUAL_STRING("Dev1", deviceData[0]->name.c_str());
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 230.0, deviceData[0]->volts);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 50.0,  deviceData[0]->hz);
}

void test_sync_data_amps_derived_from_va_over_volts() {
    devices[0] = new inputDevice(1);
    devices[0]->current.volts = 230.0;
    devices[0]->current.va    = 1150.0; // 5 A

    syncDeviceData();

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 5.0, deviceData[0]->amps);
}

void test_sync_data_amps_zero_when_voltage_zero() {
    devices[0] = new inputDevice(1);
    devices[0]->current.volts = 0.0;
    devices[0]->current.va    = 1150.0;

    syncDeviceData();

    TEST_ASSERT_EQUAL_DOUBLE(0.0, deviceData[0]->amps);
}

void test_sync_data_pf_derived_from_watts_over_va() {
    devices[0] = new inputDevice(1);
    devices[0]->current.volts = 230.0;
    devices[0]->current.va    = 1000.0;
    devices[0]->current.watts = 900.0; // pf = 0.9

    syncDeviceData();

    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.9, deviceData[0]->pf);
}

void test_sync_data_pf_zero_when_va_zero() {
    devices[0] = new inputDevice(1);
    devices[0]->current.volts = 230.0;
    devices[0]->current.va    = 0.0;
    devices[0]->current.watts = 0.0;

    syncDeviceData();

    TEST_ASSERT_EQUAL_DOUBLE(0.0, deviceData[0]->pf);
}

void test_sync_data_removes_data_when_device_null() {
    // Pre-existing data entry for a device that no longer exists.
    deviceData[0]       = new inputDeviceData{};
    deviceData[0]->name = "Gone";
    // devices[0] is nullptr.

    syncDeviceData();

    TEST_ASSERT_NULL(deviceData[0]);
}

void test_sync_data_null_device_name_gives_null_data_name() {
    devices[0] = new inputDevice(1);
    devices[0]->current.volts = 230.0;

    syncDeviceData();

    TEST_ASSERT_TRUE(deviceData[0]->name.isEmpty());
}

// ============================================================================
// syncDevices – integration (devicesChanged flag)
// ============================================================================

void test_sync_devices_clears_changed_flag() {
    devicesChanged  = true;
    auto *info      = new inputDeviceInfo(1);
    info->enabled   = true;
    info->name      = "X";
    deviceInfos[0]  = info;

    syncDevices(nullptr);

    TEST_ASSERT_FALSE(devicesChanged);
}

void test_sync_devices_returns_1000() {
    uint32_t result = syncDevices(nullptr);
    TEST_ASSERT_EQUAL(1000, result);
}

// ============================================================================
// Test runner
// ============================================================================

void setup() {
    UNITY_BEGIN();

    // syncDeviceInfo
    RUN_TEST(test_sync_info_creates_device_for_new_info);
    RUN_TEST(test_sync_info_copies_calibration_and_reversed);
    RUN_TEST(test_sync_info_updates_existing_device);
    RUN_TEST(test_sync_info_removes_device_when_info_null);
    RUN_TEST(test_sync_info_null_info_name_gives_null_device_name);

    // syncDeviceAction
    RUN_TEST(test_sync_action_forwards_control_to_data);
    RUN_TEST(test_sync_action_clears_control_after_forward);
    RUN_TEST(test_sync_action_no_op_when_none);

    // syncDeviceData
    RUN_TEST(test_sync_data_creates_data_entry_for_active_device);
    RUN_TEST(test_sync_data_amps_derived_from_va_over_volts);
    RUN_TEST(test_sync_data_amps_zero_when_voltage_zero);
    RUN_TEST(test_sync_data_pf_derived_from_watts_over_va);
    RUN_TEST(test_sync_data_pf_zero_when_va_zero);
    RUN_TEST(test_sync_data_removes_data_when_device_null);
    RUN_TEST(test_sync_data_null_device_name_gives_null_data_name);

    // syncDevices integration
    RUN_TEST(test_sync_devices_clears_changed_flag);
    RUN_TEST(test_sync_devices_returns_1000);

    UNITY_END();
}

void loop() {}

int main(int argc, char **argv) {
    setup();
    return 0;
}

