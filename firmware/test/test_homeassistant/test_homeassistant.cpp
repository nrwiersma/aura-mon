//
// Unit tests for the Home Assistant uploader's pure, hardware-independent
// helpers: settings parsing/validation and JSON payload building.
//

#include <unity.h>
#include <ArduinoJson.h>
#include "../stubs/TestPlatform.h"
#include "../../src/uploader/homeassistant_format.h"

void setUp() {
}

void tearDown() {
}

// ============================================================================
// parseHomeAssistantSettings
// ============================================================================

void test_parse_settings_valid() {
    JsonDocument doc;
    doc["url"]        = "http://homeassistant.local:8123";
    doc["webhook_id"] = "aura-mon-webhook";

    HomeAssistantSettings out;
    TEST_ASSERT_TRUE(parseHomeAssistantSettings(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_STRING("http://homeassistant.local:8123", out.url.c_str());
    TEST_ASSERT_EQUAL_STRING("aura-mon-webhook", out.webhookId.c_str());
}

void test_parse_settings_trims_trailing_slash() {
    JsonDocument doc;
    doc["url"]        = "http://homeassistant.local:8123/";
    doc["webhook_id"] = "aura-mon-webhook";

    HomeAssistantSettings out;
    TEST_ASSERT_TRUE(parseHomeAssistantSettings(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_STRING("http://homeassistant.local:8123", out.url.c_str());
}

void test_parse_settings_missing_url_rejected() {
    JsonDocument doc;
    doc["webhook_id"] = "aura-mon-webhook";

    HomeAssistantSettings out;
    TEST_ASSERT_FALSE(parseHomeAssistantSettings(doc.as<JsonObjectConst>(), out));
}

void test_parse_settings_missing_webhook_id_rejected() {
    JsonDocument doc;
    doc["url"] = "http://homeassistant.local:8123";

    HomeAssistantSettings out;
    TEST_ASSERT_FALSE(parseHomeAssistantSettings(doc.as<JsonObjectConst>(), out));
}

void test_parse_settings_empty_object_rejected() {
    JsonDocument doc;

    HomeAssistantSettings out;
    TEST_ASSERT_FALSE(parseHomeAssistantSettings(doc.as<JsonObjectConst>(), out));
}

// ============================================================================
// homeAssistantEndpoint
// ============================================================================

void test_endpoint_combines_url_and_webhook_id() {
    HomeAssistantSettings s;
    s.url       = "http://homeassistant.local:8123";
    s.webhookId = "aura-mon-webhook";

    TEST_ASSERT_EQUAL_STRING("http://homeassistant.local:8123/api/webhook/aura-mon-webhook",
                              homeAssistantEndpoint(s).c_str());
}

// ============================================================================
// setHomeAssistantEnvelope
// ============================================================================

void test_envelope_sets_ts_and_hz() {
    JsonDocument doc;
    setHomeAssistantEnvelope(doc, 1000, 0, 50, 1.0);

    TEST_ASSERT_EQUAL_UINT32(1000, doc["ts"].as<uint32_t>());
    TEST_ASSERT_EQUAL_DOUBLE(50.0, doc["hz"].as<double>());
}

void test_envelope_zero_elapsed_hours_omits_hz() {
    JsonDocument doc;
    setHomeAssistantEnvelope(doc, 1000, 0, 50, 0.0);

    TEST_ASSERT_TRUE(doc["ts"].is<uint32_t>());
    TEST_ASSERT_FALSE(doc["hz"].is<double>());
}

void test_envelope_zero_hz_omitted() {
    JsonDocument doc;
    setHomeAssistantEnvelope(doc, 1000, 0, 0, 1.0);

    TEST_ASSERT_FALSE(doc["hz"].is<double>());
}

void test_envelope_negative_hz_omitted() {
    JsonDocument doc;
    // hzHrs going backwards would produce a negative average; must not be sent.
    setHomeAssistantEnvelope(doc, 1000, 50, 0, 1.0);

    TEST_ASSERT_FALSE(doc["hz"].is<double>());
}

// ============================================================================
// appendHomeAssistantDevice
// ============================================================================

void test_device_appends_entry() {
    JsonDocument doc;
    JsonArray    devices = doc.to<JsonArray>();

    // 1 hour elapsed; 230V average, 100Wh consumed (100W average), 110VAh (110VA average).
    bool wrote = appendHomeAssistantDevice(devices, "Kitchen", 0, 230, 0, 100, 0, 110, 1.0);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_UINT32(1, devices.size());
    JsonObject device = devices[0];
    TEST_ASSERT_EQUAL_STRING("Kitchen", device["name"].as<const char *>());
    TEST_ASSERT_EQUAL_DOUBLE(230.0, device["volts"].as<double>());
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.478, device["amps"].as<double>());
    TEST_ASSERT_EQUAL_DOUBLE(100.0, device["watts"].as<double>());
    TEST_ASSERT_EQUAL_DOUBLE(100.0, device["wh"].as<double>());
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.9091, device["pf"].as<double>());
}

void test_device_zero_elapsed_hours_skipped() {
    JsonDocument doc;
    JsonArray    devices = doc.to<JsonArray>();

    bool wrote = appendHomeAssistantDevice(devices, "Kitchen", 0, 230, 0, 100, 0, 110, 0.0);

    TEST_ASSERT_FALSE(wrote);
    TEST_ASSERT_EQUAL_UINT32(0, devices.size());
}

void test_device_negative_energy_clamped_to_zero() {
    JsonDocument doc;
    JsonArray    devices = doc.to<JsonArray>();

    // wattHrs goes down (e.g. counter reset); energy should clamp to 0, not go negative.
    bool wrote = appendHomeAssistantDevice(devices, "Kitchen", 0, 230, 100, 50, 0, 110, 1.0);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, devices[0]["wh"].as<double>());
}

void test_device_zero_voltage_gives_zero_current() {
    JsonDocument doc;
    JsonArray    devices = doc.to<JsonArray>();

    bool wrote = appendHomeAssistantDevice(devices, "Kitchen", 0, 0, 0, 0, 0, 0, 1.0);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, devices[0]["amps"].as<double>());
}

void test_device_zero_apparent_power_gives_zero_power_factor() {
    JsonDocument doc;
    JsonArray    devices = doc.to<JsonArray>();

    bool wrote = appendHomeAssistantDevice(devices, "Kitchen", 0, 230, 0, 0, 0, 0, 1.0);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, devices[0]["pf"].as<double>());
}

void test_device_appends_to_existing_array() {
    JsonDocument doc;
    JsonArray    devices = doc.to<JsonArray>();
    devices.add("existing");

    bool wrote = appendHomeAssistantDevice(devices, "Kitchen", 0, 230, 0, 100, 0, 110, 1.0);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_UINT32(2, devices.size());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_settings_valid);
    RUN_TEST(test_parse_settings_trims_trailing_slash);
    RUN_TEST(test_parse_settings_missing_url_rejected);
    RUN_TEST(test_parse_settings_missing_webhook_id_rejected);
    RUN_TEST(test_parse_settings_empty_object_rejected);

    RUN_TEST(test_endpoint_combines_url_and_webhook_id);

    RUN_TEST(test_envelope_sets_ts_and_hz);
    RUN_TEST(test_envelope_zero_elapsed_hours_omits_hz);
    RUN_TEST(test_envelope_zero_hz_omitted);
    RUN_TEST(test_envelope_negative_hz_omitted);

    RUN_TEST(test_device_appends_entry);
    RUN_TEST(test_device_zero_elapsed_hours_skipped);
    RUN_TEST(test_device_negative_energy_clamped_to_zero);
    RUN_TEST(test_device_zero_voltage_gives_zero_current);
    RUN_TEST(test_device_zero_apparent_power_gives_zero_power_factor);
    RUN_TEST(test_device_appends_to_existing_array);

    return UNITY_END();
}
