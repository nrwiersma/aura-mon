//
// Unit tests for EnergyExportProducer against a real DataLog instance
// (backed by MockSD), same fixture conventions as test_datalog.
//

#include <unity.h>

#include <string>

#include "../stubs/TestCore.h"
#include "../../src/data_log.h"
#include "../../src/energy_export_producer.h"

namespace {

DataLog *testLog;

void enableDevice(uint8_t idx, const char *name) {
    auto *info = new InputDeviceInfo(idx + 1);
    info->enabled = true;
    info->name = name;
    deviceInfos[idx] = info;
}

// Drives produce() as HttpConnection would, collecting every byte streamed
// across however many calls it takes, until Done/Error.
struct Result {
    HttpResponseProducer::Status status;
    std::string body;
};

Result runToCompletion(EnergyExportProducer &p, size_t bufSize = 16) {
    Result result{};
    std::vector<uint8_t> buf(bufSize);
    size_t written = 0;
    int steps = 0;
    do {
        result.status = p.produce(buf.data(), buf.size(), written);
        if (written > 0) {
            result.body.append(reinterpret_cast<char *>(buf.data()), written);
        }
        steps++;
    } while ((result.status == HttpResponseProducer::Status::Pending ||
              result.status == HttpResponseProducer::Status::Data) &&
             steps < 10000);
    return result;
}

}  // namespace

void setUp() {
    testLog = new DataLog(5, 1); // 5 sec interval, 1 day max
    testLog->begin();

    sd.fileExists = false;
    if (sd.file) {
        delete sd.file;
        sd.file = nullptr;
    }
    sd.directories.clear();

    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            delete deviceInfos[i];
            deviceInfos[i] = nullptr;
        }
    }
}

void tearDown() {
    delete testLog;
    testLog = nullptr;
    // Recreate the log so subsequent begin() calls see a clean MockSD file.
    sd.fileExists = false;
    if (sd.file) {
        delete sd.file;
        sd.file = nullptr;
    }
}

// ============================================================================
// Empty log -> 204, no body
// ============================================================================

void test_no_entries_returns_204() {
    enableDevice(0, "Solar");

    EnergyExportProducer p(*testLog, 1000, 1010, 5);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(204, p.statusCode());
    TEST_ASSERT_EQUAL_STRING("", result.body.c_str());
}

// ============================================================================
// No enabled devices -> 204, no body
// ============================================================================

void test_no_enabled_devices_returns_204() {
    LogRecord rec;
    rec.ts = 1000;
    testLog->write(&rec);

    EnergyExportProducer p(*testLog, 1000, 1010, 5);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(204, p.statusCode());
    TEST_ASSERT_EQUAL_STRING("", result.body.c_str());
}

// ============================================================================
// start beyond the last record -> 204, no body
// ============================================================================

void test_start_after_last_ts_returns_204() {
    enableDevice(0, "Solar");
    LogRecord rec;
    rec.ts = 1000;
    testLog->write(&rec);

    EnergyExportProducer p(*testLog, 5000, 5010, 5);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(204, p.statusCode());
}

// ============================================================================
// Invalid parameters (start >= end) -> 400 error before any bytes sent
// ============================================================================

void test_invalid_parameters_returns_400() {
    enableDevice(0, "Solar");
    LogRecord rec;
    rec.ts = 1000;
    testLog->write(&rec);

    EnergyExportProducer p(*testLog, 2000, 1000, 5);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Error), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(400, p.statusCode());
}

// ============================================================================
// Header + row CSV formatting parity with the previous blocking handler
// ============================================================================

void test_streams_header_and_rows() {
    enableDevice(0, "Solar");

    LogRecord rec0;
    rec0.ts = 1000;
    rec0.logHours = 0.0;
    rec0.hzHrs = 0.0;
    rec0.voltHrs[0] = 0.0;
    rec0.wattHrs[0] = 0.0;
    rec0.vaHrs[0] = 0.0;
    testLog->write(&rec0);

    LogRecord rec1;
    rec1.ts = 1005;
    rec1.logHours = 1.0;
    rec1.hzHrs = 50.0;
    rec1.voltHrs[0] = 230.0;
    rec1.wattHrs[0] = 460.0;
    rec1.vaHrs[0] = 500.0;
    testLog->write(&rec1);

    LogRecord rec2;
    rec2.ts = 1010;
    rec2.logHours = 2.0;
    rec2.hzHrs = 100.0;
    rec2.voltHrs[0] = 460.0;
    rec2.wattHrs[0] = 920.0;
    rec2.vaHrs[0] = 1000.0;
    testLog->write(&rec2);

    EnergyExportProducer p(*testLog, 1000, 1010, 5);
    auto result = runToCompletion(p);
    TEST_ASSERT_EQUAL(static_cast<int>(HttpResponseProducer::Status::Done), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL(200, p.statusCode());

    const std::string expected =
        "timestamp,Hz,Solar.V,Solar.A,Solar.W,Solar.Wh,Solar.PF\n"
        "1005,50.00,230.000,2.174,460.000,460.000000,0.9200\n"
        "1010,50.00,230.000,2.174,460.000,460.000000,0.9200\n";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), result.body.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_no_entries_returns_204);
    RUN_TEST(test_no_enabled_devices_returns_204);
    RUN_TEST(test_start_after_last_ts_returns_204);
    RUN_TEST(test_invalid_parameters_returns_400);
    RUN_TEST(test_streams_header_and_rows);
    return UNITY_END();
}
