#include <unity.h>
#include <ArduinoJson.h>

#include "../../.pio/libdeps/native/Unity/src/unity.h"
#include "../../src/config.h"

// Helper to clean up device infos between tests
inline void cleanupDeviceInfos() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            delete deviceInfos[i];
            deviceInfos[i] = nullptr;
        }
    }
}

void setUp() {
    cleanupDeviceInfos();

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

void setup() {
    UNITY_BEGIN();

    RUN_TEST(test_config_valid);
    RUN_TEST(test_load_not_found);

    UNITY_END();
}

void loop() {
    // Nothing to do here
}

int main(int argc, char **argv) {
    setup();
    return 0;
}
