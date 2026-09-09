//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

#include <cstdlib>

#include <ImmediateResponse.h>

#include "log_truncate_producer.h"
#include "log_range_producer.h"
#include "energy_export_producer.h"
#include "static_file_producer.h"
#include "ota_upload_handler.h"
#include "public_upload_handler.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";

namespace {

// Parses a query parameter as an unsigned integer, falling back to
// `def` when the parameter is absent. Returns false (leaving `out`
// unchanged) if the parameter is present but not a valid, non-negative
// integer.
bool parseUintQueryParam(const HttpRequest &req, const char *key, uint32_t def, uint32_t &out) {
    const std::string *v = req.queryParam(key);
    if (!v) {
        out = def;
        return true;
    }
    char *end = nullptr;
    const long parsed = strtol(v->c_str(), &end, 10);
    if (end == v->c_str() || *end != '\0' || parsed < 0) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

std::unique_ptr<HttpResponseProducer> jsonError(int statusCode, const char *msg) {
    std::string body = std::string("{\"error\":\"") + msg + "\"}";
    return std::make_unique<ImmediateResponse>(statusCode, contentTypeJSON, body);
}

std::unique_ptr<HttpResponseProducer> internalError(const char *reason) {
    std::string body = std::string("{\"error\":\"Internal Error\",\"reason\":\"") + reason + "\"}";
    return std::make_unique<ImmediateResponse>(500, contentTypeJSON, body);
}

std::unique_ptr<HttpResponseProducer> returnOK() {
    return std::make_unique<ImmediateResponse>(200, contentTypePlain, "");
}

void appendCSVValue(String &row, double value, const uint8_t precision = 3) {
    row += ",";
    if (std::isfinite(value)) {
        row += String(value, precision);
    }
}

std::unique_ptr<HttpResponseProducer> handleGetConfig(const HttpRequest &) {
    JsonDocument doc;
    saveConfigJSON(doc);

    String response;
    serializeJson(doc, response);

    return std::make_unique<ImmediateResponse>(200, contentTypeJSON, response.c_str());
}

std::unique_ptr<HttpResponseProducer> handlePostConfig(const HttpRequest &req) {
    if (req.body.empty()) {
        return jsonError(400, "No data provided");
    }

    JsonDocument doc;
    if (auto err = deserializeJson(doc, req.body); err) {
        return jsonError(400, "Invalid JSON");
    }

    auto err = loadConfigJSON(doc);
    if (err) {
        std::string msg = std::string("{\"error\":\"Invalid configuration\",\"reason\":\"") + err.Error() + "\"}";
        return std::make_unique<ImmediateResponse>(400, contentTypeJSON, msg);
    }

    err = saveConfig();
    if (err) {
        return internalError(err.Error());
    }

    mutex_enter_blocking(&deviceInfoMu);
    devicesChanged = true;
    mutex_exit(&deviceInfoMu);

    return std::make_unique<ImmediateResponse>(200, contentTypePlain, "");
}

std::unique_ptr<HttpResponseProducer> handleDeviceAction(const HttpRequest &req) {
    if (req.body.empty()) {
        return jsonError(400, "No data provided");
    }

    JsonDocument doc;
    if (auto err = deserializeJson(doc, req.body); err) {
        return jsonError(400, "Invalid JSON");
    }

    if (!doc["action"].is<const char *>() || !doc["address"].is<uint32_t>()) {
        return jsonError(400, "Invalid action payload");
    }

    const char *     actionStr = doc["action"].as<const char *>();
    uint32_t         address = doc["address"].as<uint32_t>();
    DeviceActionType action = DeviceActionType::None;

    if (strcmp(actionStr, "locate") == 0) {
        action = DeviceActionType::Locate;
    } else if (strcmp(actionStr, "assign") == 0) {
        action = DeviceActionType::Assign;
    } else {
        return jsonError(400, "Unknown action");
    }

    if (address == 0 || address > MAX_DEVICES) {
        return jsonError(400, "Invalid address");
    }

    if (!mutex_enter_timeout_ms(&deviceActionMu, 100)) {
        return internalError("could not acquire deviceInfoMu");
    }

    if (deviceActionControl.type != DeviceActionType::None) {
        mutex_exit(&deviceActionMu);
        return jsonError(409, "Action already pending");
    }

    deviceActionControl = {action, static_cast<uint8_t>(address)};

    mutex_exit(&deviceActionMu);

    return std::make_unique<ImmediateResponse>(202, contentTypeJSON, "{\"status\":\"queued\"}");
}

std::unique_ptr<HttpResponseProducer> handleReboot(const HttpRequest &) {
    LOGI("Reboot requested");

    // Deferred, exactly like the OTA success path, so the 204 response has
    // already been handed off to the transport before we reset.
    c0Queue.add([](void *) -> uint32_t {
        safeReboot();
        return 0;
    }, 10);

    return std::make_unique<ImmediateResponse>(204, contentTypePlain, "");
}

std::unique_ptr<HttpResponseProducer> handleMetrics(const HttpRequest &) {
    const uint32_t errors = metrics.modbus_errors_total.load(std::memory_order_relaxed);
    const uint64_t totalMs = metrics.modbus_collect_time_ms_total.load(std::memory_order_relaxed);
    const uint32_t avgMs = metrics.modbus_last_run_avg_ms.load(std::memory_order_relaxed);
    const uint32_t collectRuns = metrics.modbus_collect_runs_total.load(std::memory_order_relaxed);
    const uint32_t datalogReadIO = metrics.datalog_read_io.load(std::memory_order_relaxed);
    const uint32_t datalogWriteIO = metrics.datalog_write_io.load(std::memory_order_relaxed);
    const uint32_t datalogWriteMsTotal = metrics.datalog_write_time_ms_total.load(std::memory_order_relaxed);
    const uint32_t datalogCacheHit = metrics.datalog_cache_hit.load(std::memory_order_relaxed);
    const uint32_t datalogWriteErrors = metrics.datalog_write_errors_total.load(std::memory_order_relaxed);
    const uint32_t datalogQueueDepth = metrics.datalog_queue_depth.load(std::memory_order_relaxed);
    const uint32_t datalogQueueFull = metrics.datalog_queue_full_total.load(std::memory_order_relaxed);
    const uint32_t datalogDropped = metrics.datalog_records_dropped_total.load(std::memory_order_relaxed);
    const uint32_t ntpSyncs = metrics.ntp_syncs_total.load(std::memory_order_relaxed);
    const uint32_t ntpFailures = metrics.ntp_failures_total.load(std::memory_order_relaxed);
    const int32_t  ntpOffsetMs = metrics.ntp_last_offset_ms.load(std::memory_order_relaxed);
    const uint32_t ethDisconnects = metrics.ethernet_disconnects_total.load(std::memory_order_relaxed);
    const uint32_t logErrors = metrics.log_errors_total.load(std::memory_order_relaxed);

    String response;
    response.reserve(4096);

    // Modbus metrics.
    response += F("# HELP auramon_modbus_errors_total Total modbus collection errors.\n");
    response += F("# TYPE auramon_modbus_errors_total counter\n");
    response += F("auramon_modbus_errors_total ");
    response += String(errors);
    response += '\n';
    response += F("# HELP auramon_collect_time_seconds_total Total time spent collecting data in seconds.\n");
    response += F("# TYPE auramon_collect_time_seconds_total counter\n");
    response += F("auramon_collect_time_seconds_total ");
    response += String(totalMs / 1000.0, 6);
    response += '\n';
    response += F("# HELP auramon_collect_time_seconds_avg Average per-device collection time for the last run in seconds.\n");
    response += F("# TYPE auramon_collect_time_seconds_avg gauge\n");
    response += F("auramon_collect_time_seconds_avg ");
    response += String(avgMs / 1000.0, 6);
    response += '\n';
    response += F("# HELP auramon_modbus_collect_runs_total Total number of Modbus collection cycles run.\n");
    response += F("# TYPE auramon_modbus_collect_runs_total counter\n");
    response += F("auramon_modbus_collect_runs_total ");
    response += String(collectRuns);
    response += '\n';

    // Per-device Modbus metrics.
    response += F("# HELP auramon_modbus_device_errors_total Total errors per Modbus device.\n");
    response += F("# TYPE auramon_modbus_device_errors_total counter\n");
    mutex_enter_blocking(&deviceInfoMu);
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        const auto info = deviceInfos[i];
        if (!info || !info->isEnabled()) continue;
        const uint32_t devErrors = metrics.modbus_device_errors_total[i].load(std::memory_order_relaxed);
        response += F("auramon_modbus_device_errors_total{device=\"");
        response += info->name;
        response += F("\",address=\"");
        response += String(info->addr);
        response += F("\"} ");
        response += String(devErrors);
        response += '\n';
    }
    response += F("# HELP auramon_modbus_device_collect_time_seconds Last successful collection time per Modbus device in seconds.\n");
    response += F("# TYPE auramon_modbus_device_collect_time_seconds gauge\n");
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        const auto info = deviceInfos[i];
        if (!info || !info->isEnabled()) continue;
        const uint32_t devMs = metrics.modbus_device_last_collect_ms[i].load(std::memory_order_relaxed);
        response += F("auramon_modbus_device_collect_time_seconds{device=\"");
        response += info->name;
        response += F("\",address=\"");
        response += String(info->addr);
        response += F("\"} ");
        response += String(devMs / 1000.0, 6);
        response += '\n';
    }
    mutex_exit(&deviceInfoMu);

    // Datalog metrics.
    response += F("# HELP auramon_datalog_read_io Number of read IO operations performed on the datalog.\n");
    response += F("# TYPE auramon_datalog_read_io counter\n");
    response += F("auramon_datalog_read_io ");
    response += String(datalogReadIO);
    response += '\n';
    response += F("# HELP auramon_datalog_write_io Number of write IO operations performed on the datalog.\n");
    response += F("# TYPE auramon_datalog_write_io counter\n");
    response += F("auramon_datalog_write_io ");
    response += String(datalogWriteIO);
    response += '\n';
    response += F("# HELP auramon_datalog_write_time_seconds_total Total time spent writing datalog records in seconds.\n");
    response += F("# TYPE auramon_datalog_write_time_seconds_total counter\n");
    response += F("auramon_datalog_write_time_seconds_total ");
    response += String(datalogWriteMsTotal / 1000.0, 6);
    response += '\n';
    response += F("# HELP auramon_datalog_cache_hit Number of cache hits when reading records from the datalog.\n");
    response += F("# TYPE auramon_datalog_cache_hit counter\n");
    response += F("auramon_datalog_cache_hit ");
    response += String(datalogCacheHit);
    response += '\n';
    response += F("# HELP auramon_datalog_write_errors_total Total failed datalog write attempts.\n");
    response += F("# TYPE auramon_datalog_write_errors_total counter\n");
    response += F("auramon_datalog_write_errors_total ");
    response += String(datalogWriteErrors);
    response += '\n';
    response += F("# HELP auramon_datalog_queue_depth Number of records waiting to be written to the datalog.\n");
    response += F("# TYPE auramon_datalog_queue_depth gauge\n");
    response += F("auramon_datalog_queue_depth ");
    response += String(datalogQueueDepth);
    response += '\n';
    response += F("# HELP auramon_datalog_queue_full_total Total times a record could not be queued because the queue was full.\n");
    response += F("# TYPE auramon_datalog_queue_full_total counter\n");
    response += F("auramon_datalog_queue_full_total ");
    response += String(datalogQueueFull);
    response += '\n';
    response += F("# HELP auramon_datalog_records_dropped_total Total records dropped after repeated write failures.\n");
    response += F("# TYPE auramon_datalog_records_dropped_total counter\n");
    response += F("auramon_datalog_records_dropped_total ");
    response += String(datalogDropped);
    response += '\n';

    // NTP metrics.
    response += F("# HELP auramon_ntp_syncs_total Total successful NTP synchronisations.\n");
    response += F("# TYPE auramon_ntp_syncs_total counter\n");
    response += F("auramon_ntp_syncs_total ");
    response += String(ntpSyncs);
    response += '\n';
    response += F("# HELP auramon_ntp_failures_total Total failed NTP synchronisation attempts.\n");
    response += F("# TYPE auramon_ntp_failures_total counter\n");
    response += F("auramon_ntp_failures_total ");
    response += String(ntpFailures);
    response += '\n';
    response += F("# HELP auramon_ntp_offset_ms Clock offset applied during the last NTP sync in milliseconds.\n");
    response += F("# TYPE auramon_ntp_offset_ms gauge\n");
    response += F("auramon_ntp_offset_ms ");
    response += String((long)ntpOffsetMs);
    response += '\n';

    // Network metrics.
    response += F("# HELP auramon_ethernet_disconnects_total Total number of Ethernet link-loss events.\n");
    response += F("# TYPE auramon_ethernet_disconnects_total counter\n");
    response += F("auramon_ethernet_disconnects_total ");
    response += String(ethDisconnects);
    response += '\n';

    // Logging metrics.
    response += F("# HELP auramon_log_errors_total Total number of error-level log messages emitted.\n");
    response += F("# TYPE auramon_log_errors_total counter\n");
    response += F("auramon_log_errors_total ");
    response += String(logErrors);
    response += '\n';

    return std::make_unique<ImmediateResponse>(200, contentTypePlain, response.c_str());
}

std::unique_ptr<HttpResponseProducer> handleStatus(const HttpRequest &) {
    JsonDocument doc;

    doc["version"] = AURAMON_VERSION;

    JsonObject statsObj = doc["stats"].to<JsonObject>();
    statsObj["startTime"] = startTime;
    statsObj["currentTime"] = time(nullptr);
    statsObj["runSeconds"] = time(nullptr) - startTime;
    statsObj["heapFree"] = rp2040.getFreeHeap();

    JsonArray devicesArr = doc["devices"].to<JsonArray>();

    mutex_enter_blocking(&deviceDataMu);

    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        auto data = deviceData[i];
        if (!data || data->name.isEmpty()) {
            continue;
        }

        auto deviceObj = devicesArr.add<JsonObject>();
        deviceObj["name"] = data->name;
        deviceObj["volts"] = data->volts;
        deviceObj["amps"] = data->amps;
        deviceObj["pf"] = data->pf;
        deviceObj["hz"] = data->hz;
    }
    mutex_exit(&deviceDataMu);

    JsonObject datalogObj = doc["datalog"].to<JsonObject>();
    datalogObj["firstRev"] = datalog.firstRev();
    datalogObj["firstTS"] = datalog.firstTS();
    datalogObj["lastRev"] = datalog.lastRev();
    datalogObj["lastTS"] = datalog.lastTS();
    datalogObj["interval"] = datalog.interval();
    datalogObj["size"] = datalog.fileSize();

    JsonObject networkObj = doc["network"].to<JsonObject>();
    networkObj["hostname"] = netCfg.hostname;
    networkObj["ip"] = eth.localIP().toString();
    networkObj["gateway"] = eth.gatewayIP().toString();
    networkObj["subnet"] = eth.subnetMask().toString();
    networkObj["dns"] = eth.dnsIP().toString();
    char mac_str[18];
    snprintf_P(mac_str, sizeof(mac_str), PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    networkObj["mac"] = mac_str;

    String response;
    serializeJson(doc, response);

    return std::make_unique<ImmediateResponse>(200, contentTypeJSON, response.c_str());
}

std::unique_ptr<HttpResponseProducer> handleEnergy(const HttpRequest &req) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t interval = 0;
    if (!parseUintQueryParam(req, "start", 0, start) ||
        !parseUintQueryParam(req, "end", static_cast<uint32_t>(time(nullptr)), end) ||
        !parseUintQueryParam(req, "interval", 5, interval)) {
        return jsonError(400, "Invalid parameters");
    }

    LOGD("Energy request start=%u end=%u interval=%u", start, end, interval);

    return std::make_unique<EnergyExportProducer>(datalog, start, end, interval);
}

std::unique_ptr<HttpResponseProducer> handleLogs(const HttpRequest &req) {
    uint32_t startOffset = 0;
    if (!parseUintQueryParam(req, "start", 0, startOffset)) {
        return jsonError(400, "Invalid start");
    }
    uint32_t limitBytes = 0;
    if (req.queryParam("limit")) {
        if (!parseUintQueryParam(req, "limit", 0, limitBytes) || limitBytes == 0) {
            return jsonError(400, "Invalid limit");
        }
    }

    return std::make_unique<LogRangeProducer>(startOffset, limitBytes);
}

std::unique_ptr<HttpResponseProducer> handleLogsTrunc(const HttpRequest &) {
    LOGI("Log truncation requested");
    return std::make_unique<LogTruncateProducer>();
}

}  // namespace

void setupAPI() {
    router.on(HttpMethod::GET, "/config", handleGetConfig);
    router.on(HttpMethod::POST, "/config", handlePostConfig);
    router.on(HttpMethod::GET, "/status", handleStatus);
    router.on(HttpMethod::GET, "/energy", handleEnergy);
    router.on(HttpMethod::POST, "/device/action", handleDeviceAction);
    router.on(HttpMethod::GET, "/logs", handleLogs);
    router.on(HttpMethod::POST, "/logs/trunc", handleLogsTrunc);
    router.on(HttpMethod::GET, "/metrics", handleMetrics);
    router.on(HttpMethod::POST, "/reboot", handleReboot);
    router.on(HttpMethod::GET, "/readyz", [](const HttpRequest &) { return returnOK(); });
    router.on(HttpMethod::GET, "/livez", [](const HttpRequest &) { return returnOK(); });

    router.onUpload(HttpMethod::POST, "/ota", [](const HttpRequest &) {
        return std::make_unique<OtaUploadHandler>();
    });
    router.onUpload(HttpMethod::POST, "/ota/public", [](const HttpRequest &) {
        return std::make_unique<PublicUploadHandler>();
    });

    // Serve "public/" from the SD card for anything else.
    router.onNotFound([](const HttpRequest &req) -> std::unique_ptr<HttpResponseProducer> {
        return std::make_unique<StaticFileProducer>(req.method, req.path);
    });
}
