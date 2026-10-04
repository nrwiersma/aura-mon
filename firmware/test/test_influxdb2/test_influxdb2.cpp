//
// Unit tests for the InfluxDB2 uploader's pure, hardware-independent helpers:
// settings parsing/validation and line-protocol formatting.
//

#include <unity.h>
#include <ArduinoJson.h>
#include "../stubs/TestPlatform.h"
#include "../../src/uploader/influxdb2_format.h"

void setUp() {
}

void tearDown() {
}

// ============================================================================
// parseInfluxDB2Settings
// ============================================================================

void test_parse_settings_valid() {
    JsonDocument doc;
    doc["url"]         = "http://influx.local:8086";
    doc["org"]         = "home";
    doc["bucket"]      = "energy";
    doc["token"]       = "secret-token";
    doc["measurement"] = "power";

    InfluxDB2Settings out;
    TEST_ASSERT_TRUE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_STRING("http://influx.local:8086", out.url.c_str());
    TEST_ASSERT_EQUAL_STRING("home", out.org.c_str());
    TEST_ASSERT_EQUAL_STRING("energy", out.bucket.c_str());
    TEST_ASSERT_EQUAL_STRING("secret-token", out.token.c_str());
    TEST_ASSERT_EQUAL_STRING("power", out.measurement.c_str());
}

void test_parse_settings_defaults_measurement() {
    JsonDocument doc;
    doc["url"]    = "http://influx.local:8086";
    doc["org"]    = "home";
    doc["bucket"] = "energy";
    doc["token"]  = "secret-token";

    InfluxDB2Settings out;
    TEST_ASSERT_TRUE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_STRING("aura-mon", out.measurement.c_str());
}

void test_parse_settings_trims_trailing_slash() {
    JsonDocument doc;
    doc["url"]    = "http://influx.local:8086/";
    doc["org"]    = "home";
    doc["bucket"] = "energy";
    doc["token"]  = "secret-token";

    InfluxDB2Settings out;
    TEST_ASSERT_TRUE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
    TEST_ASSERT_EQUAL_STRING("http://influx.local:8086", out.url.c_str());
}

void test_parse_settings_missing_url_rejected() {
    JsonDocument doc;
    doc["org"]    = "home";
    doc["bucket"] = "energy";
    doc["token"]  = "secret-token";

    InfluxDB2Settings out;
    TEST_ASSERT_FALSE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
}

void test_parse_settings_missing_org_rejected() {
    JsonDocument doc;
    doc["url"]    = "http://influx.local:8086";
    doc["bucket"] = "energy";
    doc["token"]  = "secret-token";

    InfluxDB2Settings out;
    TEST_ASSERT_FALSE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
}

void test_parse_settings_missing_bucket_rejected() {
    JsonDocument doc;
    doc["url"]   = "http://influx.local:8086";
    doc["org"]   = "home";
    doc["token"] = "secret-token";

    InfluxDB2Settings out;
    TEST_ASSERT_FALSE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
}

void test_parse_settings_missing_token_rejected() {
    JsonDocument doc;
    doc["url"]    = "http://influx.local:8086";
    doc["org"]    = "home";
    doc["bucket"] = "energy";

    InfluxDB2Settings out;
    TEST_ASSERT_FALSE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
}

void test_parse_settings_empty_object_rejected() {
    JsonDocument doc;

    InfluxDB2Settings out;
    TEST_ASSERT_FALSE(parseInfluxDB2Settings(doc.as<JsonObjectConst>(), out));
}

// ============================================================================
// influxDB2Endpoint
// ============================================================================

void test_endpoint_combines_url_org_bucket() {
    InfluxDB2Settings s;
    s.url    = "http://influx.local:8086";
    s.org    = "home";
    s.bucket = "energy";

    TEST_ASSERT_EQUAL_STRING("http://influx.local:8086/api/v2/write?precision=s&org=home&bucket=energy",
                              influxDB2Endpoint(s).c_str());
}

// ============================================================================
// influxDB2EscapeTagValue
// ============================================================================

void test_escape_tag_value_no_special_chars() {
    TEST_ASSERT_EQUAL_STRING("Kitchen", influxDB2EscapeTagValue("Kitchen").c_str());
}

void test_escape_tag_value_space() {
    TEST_ASSERT_EQUAL_STRING("Living\\ Room", influxDB2EscapeTagValue("Living Room").c_str());
}

void test_escape_tag_value_comma() {
    TEST_ASSERT_EQUAL_STRING("a\\,b", influxDB2EscapeTagValue("a,b").c_str());
}

void test_escape_tag_value_equals() {
    TEST_ASSERT_EQUAL_STRING("a\\=b", influxDB2EscapeTagValue("a=b").c_str());
}

void test_escape_tag_value_mixed() {
    TEST_ASSERT_EQUAL_STRING("a\\,b\\=c\\ d", influxDB2EscapeTagValue("a,b=c d").c_str());
}

void test_escape_tag_value_empty() {
    TEST_ASSERT_EQUAL_STRING("", influxDB2EscapeTagValue("").c_str());
}

// ============================================================================
// appendInfluxDB2DevicePoint
// ============================================================================

void test_device_point_appends_line() {
    String body;
    // 1 hour elapsed; 230V average, 100Wh consumed (100W average), 110VAh (110VA average).
    bool wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Kitchen", 0, 230, 0, 100, 0, 110, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_STRING(
        "aura-mon,device=Kitchen volts=230.00,amps=0.478,watts=100.00,va=110.00,wh=100.000000,pf=0.9091 1000\n",
        body.c_str());
}

void test_device_point_escapes_device_name() {
    String body;
    bool   wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Living Room", 0, 230, 0, 100, 0, 110, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_TRUE(body.indexOf("device=Living\\ Room") >= 0);
}

void test_device_point_zero_elapsed_hours_skipped() {
    String body;
    bool   wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Kitchen", 0, 230, 0, 100, 0, 110, 0.0, 1000);

    TEST_ASSERT_FALSE(wrote);
    TEST_ASSERT_TRUE(body.isEmpty());
}

void test_device_point_negative_energy_clamped_to_zero() {
    String body;
    // wattHrs goes down (e.g. counter reset); energy should clamp to 0, not go negative.
    bool wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Kitchen", 0, 230, 100, 50, 0, 110, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_TRUE(body.indexOf("wh=0.000000") >= 0);
}

void test_device_point_zero_voltage_gives_zero_current() {
    String body;
    bool   wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Kitchen", 0, 0, 0, 0, 0, 0, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_TRUE(body.indexOf("amps=0.000") >= 0);
}

void test_device_point_zero_apparent_power_gives_zero_power_factor() {
    String body;
    bool   wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Kitchen", 0, 230, 0, 0, 0, 0, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_TRUE(body.indexOf("pf=0.0000") >= 0);
}

void test_device_point_appends_to_existing_body() {
    String body = "existing\n";
    bool   wrote = appendInfluxDB2DevicePoint(body, "aura-mon", "Kitchen", 0, 230, 0, 100, 0, 110, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_TRUE(body.startsWith("existing\n"));
}

// ============================================================================
// appendInfluxDB2FrequencyPoint
// ============================================================================

void test_frequency_point_appends_line() {
    String body;
    bool   wrote = appendInfluxDB2FrequencyPoint(body, "aura-mon", 0, 50, 1.0, 1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_EQUAL_STRING("aura-mon,device=mains hz=50.00 1000\n", body.c_str());
}

void test_frequency_point_zero_elapsed_hours_skipped() {
    String body;
    bool   wrote = appendInfluxDB2FrequencyPoint(body, "aura-mon", 0, 50, 0.0, 1000);

    TEST_ASSERT_FALSE(wrote);
    TEST_ASSERT_TRUE(body.isEmpty());
}

void test_frequency_point_zero_hz_skipped() {
    String body;
    bool   wrote = appendInfluxDB2FrequencyPoint(body, "aura-mon", 0, 0, 1.0, 1000);

    TEST_ASSERT_FALSE(wrote);
    TEST_ASSERT_TRUE(body.isEmpty());
}

void test_frequency_point_negative_hz_skipped() {
    String body;
    // hzHrs going backwards would produce a negative average; must not be sent.
    bool wrote = appendInfluxDB2FrequencyPoint(body, "aura-mon", 50, 0, 1.0, 1000);

    TEST_ASSERT_FALSE(wrote);
    TEST_ASSERT_TRUE(body.isEmpty());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_settings_valid);
    RUN_TEST(test_parse_settings_defaults_measurement);
    RUN_TEST(test_parse_settings_trims_trailing_slash);
    RUN_TEST(test_parse_settings_missing_url_rejected);
    RUN_TEST(test_parse_settings_missing_org_rejected);
    RUN_TEST(test_parse_settings_missing_bucket_rejected);
    RUN_TEST(test_parse_settings_missing_token_rejected);
    RUN_TEST(test_parse_settings_empty_object_rejected);

    RUN_TEST(test_endpoint_combines_url_org_bucket);

    RUN_TEST(test_escape_tag_value_no_special_chars);
    RUN_TEST(test_escape_tag_value_space);
    RUN_TEST(test_escape_tag_value_comma);
    RUN_TEST(test_escape_tag_value_equals);
    RUN_TEST(test_escape_tag_value_mixed);
    RUN_TEST(test_escape_tag_value_empty);

    RUN_TEST(test_device_point_appends_line);
    RUN_TEST(test_device_point_escapes_device_name);
    RUN_TEST(test_device_point_zero_elapsed_hours_skipped);
    RUN_TEST(test_device_point_negative_energy_clamped_to_zero);
    RUN_TEST(test_device_point_zero_voltage_gives_zero_current);
    RUN_TEST(test_device_point_zero_apparent_power_gives_zero_power_factor);
    RUN_TEST(test_device_point_appends_to_existing_body);

    RUN_TEST(test_frequency_point_appends_line);
    RUN_TEST(test_frequency_point_zero_elapsed_hours_skipped);
    RUN_TEST(test_frequency_point_zero_hz_skipped);
    RUN_TEST(test_frequency_point_negative_hz_skipped);

    return UNITY_END();
}
