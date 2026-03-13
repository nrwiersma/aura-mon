#include <unity.h>
#include <ArduinoJson.h>

#include "../../.pio/libdeps/native/Unity/src/unity.h"
#include "../../src/config.h"

// Helper to clean up device infos between tests
inline void cleanupDeviceInfos() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            free(const_cast<char *>(deviceInfos[i]->name));
            deviceInfos[i]->name = nullptr;
            delete deviceInfos[i];
            deviceInfos[i] = nullptr;
        }
    }
}

void setUp() {
    cleanupDeviceInfos();

    // Reset network config to defaults between tests.
    netCfg.hostname = "aura-mon";
    netCfg.ip       = "";
    netCfg.gateway  = "";
    netCfg.mask     = "255.255.255.0";
    netCfg.dns      = "8.8.8.8";

    sd.fileExists = false;
    if (sd.file) {
        delete sd.file;
        sd.file = nullptr;
    }
    sd.directories.clear();
}

void tearDown() {
}

void test_config_valid() {
    JsonDocument doc;
    doc["format"] = 1;
    auto net = doc["network"].to<JsonObject>();
    net["hostname"] = "test";
    auto devices = doc["devices"].to<JsonArray>();
    auto dev1 = devices.add<JsonObject>();
    dev1["enabled"] = true;
    dev1["address"] = 1;
    dev1["name"] = "Device1";

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("test", netCfg.hostname.c_str());
    TEST_ASSERT_NOT_NULL(deviceInfos[0]);
    TEST_ASSERT_EQUAL(true, deviceInfos[0]->enabled);
    TEST_ASSERT_EQUAL(1, deviceInfos[0]->addr);
    TEST_ASSERT_EQUAL_STRING("Device1", deviceInfos[0]->name);
}

void test_load_not_found() {
    sd.fileExists = false;

    auto err = loadConfig();

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("could not decode config file", err->Error());
}

// ============================================================================
// Format version
// ============================================================================

void test_config_format_mismatch_returns_error() {
    JsonDocument doc;
    doc["format"] = 99; // unknown version

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("config format mismatch", err->Error());
}

void test_config_missing_format_field_is_accepted() {
    JsonDocument doc;
    // No "format" key at all – should be treated as valid (no version check).
    auto net = doc["network"].to<JsonObject>();
    net["hostname"] = "myhost";

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NULL(err);
}

void test_config_empty_doc_returns_error() {
    JsonDocument doc;

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("config object is empty", err->Error());
}

// ============================================================================
// Network IP validation
// ============================================================================

void test_config_invalid_ip_returns_error() {
    JsonDocument doc;
    doc["format"] = 1;
    auto net = doc["network"].to<JsonObject>();
    net["ip"] = "not.an.ip";

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("invalid ip address", err->Error());
}

void test_config_invalid_gateway_returns_error() {
    JsonDocument doc;
    doc["format"] = 1;
    auto net = doc["network"].to<JsonObject>();
    net["gateway"] = "300.1.2.3";

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("invalid gateway address", err->Error());
}

void test_config_invalid_mask_returns_error() {
    JsonDocument doc;
    doc["format"] = 1;
    auto net = doc["network"].to<JsonObject>();
    net["mask"] = "bad";

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("invalid ip mask", err->Error());
}

void test_config_invalid_dns_returns_error() {
    JsonDocument doc;
    doc["format"] = 1;
    auto net = doc["network"].to<JsonObject>();
    net["dns"] = "1.2.3.999";

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("invalid dns address", err->Error());
}

void test_config_empty_ip_string_is_accepted() {
    JsonDocument doc;
    doc["format"] = 1;
    auto net = doc["network"].to<JsonObject>();
    net["ip"] = ""; // empty string – DHCP mode, no validation needed

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NULL(err);
}

// ============================================================================
// Device field defaults
// ============================================================================

void test_config_device_calibration_defaults_to_one() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    auto d    = devs.add<JsonObject>();
    d["enabled"] = true;
    d["address"] = 1;
    // No "calibration" field.

    loadConfigJSON(doc);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, deviceInfos[0]->calibration);
}

void test_config_device_reversed_defaults_to_false() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    auto d    = devs.add<JsonObject>();
    d["enabled"] = true;
    d["address"] = 2;
    // No "reversed" field.

    loadConfigJSON(doc);

    TEST_ASSERT_FALSE(deviceInfos[1]->reversed);
}

void test_config_device_enabled_defaults_to_false() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    auto d    = devs.add<JsonObject>();
    d["address"] = 3;
    // No "enabled" field.

    loadConfigJSON(doc);

    TEST_ASSERT_FALSE(deviceInfos[2]->enabled);
}

// ============================================================================
// Round-trip: saveConfigJSON → loadConfigJSON
// ============================================================================

void test_config_round_trip_network() {
    // Set up state.
    netCfg.hostname = "roundtrip-host";
    netCfg.ip       = "";
    netCfg.gateway  = "";
    netCfg.mask     = "255.255.255.0";
    netCfg.dns      = "8.8.8.8";

    JsonDocument saved;
    saveConfigJSON(saved);

    // Reset and reload.
    netCfg.hostname = "";
    netCfg.mask     = "";

    auto err = loadConfigJSON(saved);

    TEST_ASSERT_NULL(err);
    TEST_ASSERT_EQUAL_STRING("roundtrip-host", netCfg.hostname.c_str());
    TEST_ASSERT_EQUAL_STRING("255.255.255.0",  netCfg.mask.c_str());
    TEST_ASSERT_EQUAL_STRING("8.8.8.8",        netCfg.dns.c_str());
}

void test_config_round_trip_device() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    auto d    = devs.add<JsonObject>();
    d["enabled"]     = true;
    d["address"]     = 4;
    d["name"]        = "Meter4";
    d["calibration"] = 1.05f;
    d["reversed"]    = true;

    loadConfigJSON(doc);

    JsonDocument saved;
    saveConfigJSON(saved);

    cleanupDeviceInfos();

    auto err = loadConfigJSON(saved);
    TEST_ASSERT_NULL(err);

    TEST_ASSERT_NOT_NULL(deviceInfos[3]);
    TEST_ASSERT_EQUAL_STRING("Meter4",  deviceInfos[3]->name);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.05f, deviceInfos[3]->calibration);
    TEST_ASSERT_TRUE(deviceInfos[3]->reversed);
}

// ============================================================================
// Devices array – edge cases
// ============================================================================

void test_config_device_with_address_zero_is_ignored() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    auto d    = devs.add<JsonObject>();
    d["enabled"] = true;
    d["address"] = 0; // invalid address

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NULL(err);
    // Nothing should have been created.
    for (int i = 0; i < MAX_DEVICES; i++) {
        TEST_ASSERT_NULL(deviceInfos[i]);
    }
}

void test_config_device_with_address_above_max_is_ignored() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    auto d    = devs.add<JsonObject>();
    d["enabled"] = true;
    d["address"] = MAX_DEVICES + 1;

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NULL(err);
    for (int i = 0; i < MAX_DEVICES; i++) {
        TEST_ASSERT_NULL(deviceInfos[i]);
    }
}

void test_config_multiple_devices_loaded() {
    JsonDocument doc;
    doc["format"] = 1;
    auto devs = doc["devices"].to<JsonArray>();
    for (int i = 1; i <= 3; i++) {
        auto d    = devs.add<JsonObject>();
        d["enabled"] = true;
        d["address"] = i;
    }

    auto err = loadConfigJSON(doc);

    TEST_ASSERT_NULL(err);
    TEST_ASSERT_NOT_NULL(deviceInfos[0]);
    TEST_ASSERT_NOT_NULL(deviceInfos[1]);
    TEST_ASSERT_NOT_NULL(deviceInfos[2]);
}

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_config_valid);
    RUN_TEST(test_load_not_found);

    // Format version
    RUN_TEST(test_config_format_mismatch_returns_error);
    RUN_TEST(test_config_missing_format_field_is_accepted);
    RUN_TEST(test_config_empty_doc_returns_error);

    // IP validation
    RUN_TEST(test_config_invalid_ip_returns_error);
    RUN_TEST(test_config_invalid_gateway_returns_error);
    RUN_TEST(test_config_invalid_mask_returns_error);
    RUN_TEST(test_config_invalid_dns_returns_error);
    RUN_TEST(test_config_empty_ip_string_is_accepted);

    // Device defaults
    RUN_TEST(test_config_device_calibration_defaults_to_one);
    RUN_TEST(test_config_device_reversed_defaults_to_false);
    RUN_TEST(test_config_device_enabled_defaults_to_false);

    // Round-trip
    RUN_TEST(test_config_round_trip_network);
    RUN_TEST(test_config_round_trip_device);

    // Edge cases
    RUN_TEST(test_config_device_with_address_zero_is_ignored);
    RUN_TEST(test_config_device_with_address_above_max_is_ignored);
    RUN_TEST(test_config_multiple_devices_loaded);

    UNITY_END();
}

void loop() {
    // Nothing to do here
}

int main(int argc, char **argv) {
    setup();
    return 0;
}
