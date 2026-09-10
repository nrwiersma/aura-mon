//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

#include <cstdlib>

#include "energy_export_job.h"
#include "file_stream_job.h"
#include "ota_upload_handler.h"
#include "public_upload_handler.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";

namespace {

constexpr char kRestartMarker[] = "**** RESTART ****";

void serveStaticFile(HttpRequest &req);
void truncateMessageLog(HttpRequest &req);

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

void jsonError(HttpRequest &req, int statusCode, const char *msg) {
    std::string body = std::string("{\"error\":\"") + msg + "\"}";
    req.send(statusCode, contentTypeJSON, body);
}

void internalError(HttpRequest &req, const char *reason) {
    std::string body = std::string("{\"error\":\"Internal Error\",\"reason\":\"") + reason + "\"}";
    req.send(500, contentTypeJSON, body);
}

void handleGetConfig(HttpRequest &req) {
    JsonDocument doc;
    saveConfigJSON(doc);

    String response;
    serializeJson(doc, response);

    req.send(200, contentTypeJSON, response.c_str());
}

void handlePostConfig(HttpRequest &req) {
    if (req.body.empty()) {
        jsonError(req, 400, "No data provided");
        return;
    }

    JsonDocument doc;
    if (auto err = deserializeJson(doc, req.body); err) {
        jsonError(req, 400, "Invalid JSON");
        return;
    }

    auto err = loadConfigJSON(doc);
    if (err) {
        std::string msg = std::string("{\"error\":\"Invalid configuration\",\"reason\":\"") + err.Error() + "\"}";
        req.send(400, contentTypeJSON, msg);
        return;
    }

    err = saveConfig();
    if (err) {
        internalError(req, err.Error());
        return;
    }

    mutex_enter_blocking(&deviceInfoMu);
    devicesChanged = true;
    mutex_exit(&deviceInfoMu);

    req.send(200, contentTypePlain, "");
}

void handleDeviceAction(HttpRequest &req) {
    if (req.body.empty()) {
        jsonError(req, 400, "No data provided");
        return;
    }

    JsonDocument doc;
    if (auto err = deserializeJson(doc, req.body); err) {
        jsonError(req, 400, "Invalid JSON");
        return;
    }

    if (!doc["action"].is<const char *>() || !doc["address"].is<uint32_t>()) {
        jsonError(req, 400, "Invalid action payload");
        return;
    }

    const char *     actionStr = doc["action"].as<const char *>();
    uint32_t         address = doc["address"].as<uint32_t>();
    DeviceActionType action = DeviceActionType::None;

    if (strcmp(actionStr, "locate") == 0) {
        action = DeviceActionType::Locate;
    } else if (strcmp(actionStr, "assign") == 0) {
        action = DeviceActionType::Assign;
    } else {
        jsonError(req, 400, "Unknown action");
        return;
    }

    if (address == 0 || address > MAX_DEVICES) {
        jsonError(req, 400, "Invalid address");
        return;
    }

    if (!mutex_enter_timeout_ms(&deviceActionMu, 100)) {
        internalError(req, "could not acquire deviceInfoMu");
        return;
    }

    if (deviceActionControl.type != DeviceActionType::None) {
        mutex_exit(&deviceActionMu);
        jsonError(req, 409, "Action already pending");
        return;
    }

    deviceActionControl = {action, static_cast<uint8_t>(address)};

    mutex_exit(&deviceActionMu);

    req.send(202, contentTypeJSON, "{\"status\":\"queued\"}");
}

void handleReboot(HttpRequest &req) {
    LOGI("Reboot requested");

    // Deferred, exactly like the OTA success path, so the 204 response has
    // already been handed off to the transport before we reset.
    c0Queue.add([](void *) -> uint32_t {
        safeReboot();
        return 0;
    }, 10);

    req.send(204, contentTypePlain, "");
}

void handleMetrics(HttpRequest &req) {
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

    req.send(200, contentTypePlain, response.c_str());
}

void handleStatus(HttpRequest &req) {
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

    req.send(200, contentTypeJSON, response.c_str());
}

void handleEnergy(HttpRequest &req) {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t interval = 0;
    if (!parseUintQueryParam(req, "start", 0, start) ||
        !parseUintQueryParam(req, "end", static_cast<uint32_t>(time(nullptr)), end) ||
        !parseUintQueryParam(req, "interval", 5, interval)) {
        jsonError(req, 400, "Invalid parameters");
        return;
    }

    LOGD("Energy request start=%u end=%u interval=%u", start, end, interval);

    auto *job = EnergyExportJob::start(req, datalog, start, end, interval);
    if (job) {
        c0Queue.add(&EnergyExportJob::stepTask, 5, job);
    }
}

void handleLogs(HttpRequest &req) {
    uint32_t startOffset = 0;
    if (!parseUintQueryParam(req, "start", 0, startOffset)) {
        jsonError(req, 400, "Invalid start");
        return;
    }
    uint32_t limitBytes = 0;
    if (req.queryParam("limit")) {
        if (!parseUintQueryParam(req, "limit", 0, limitBytes) || limitBytes == 0) {
            jsonError(req, 400, "Invalid limit");
            return;
        }
    }

    auto *job = FileStreamJob::open(req, MESSAGE_LOG_PATH,
                                    {.startOffset = startOffset, .limitBytes = limitBytes});
    if (job) {
        c0Queue.add(&FileStreamJob::stepTask, 5, job);
    }
}

void handleLogsTrunc(HttpRequest &req) {
    LOGI("Log truncation requested");
    truncateMessageLog(req);
}

// ---- static file serving (SD public/ dir) ----

bool endsWith(const std::string &s, const char *suffix) {
    size_t len = strlen(suffix);
    return s.size() >= len && s.compare(s.size() - len, len, suffix) == 0;
}

const char *contentTypeForPath(const std::string &path) {
    if (endsWith(path, ".html") || endsWith(path, ".html.gz")) return "text/html";
    if (endsWith(path, ".css") || endsWith(path, ".css.gz")) return "text/css";
    if (endsWith(path, ".js") || endsWith(path, ".js.gz")) return "application/javascript";
    if (endsWith(path, ".json") || endsWith(path, ".json.gz")) return "application/json";
    if (endsWith(path, ".png") || endsWith(path, ".png.gz")) return "image/png";
    if (endsWith(path, ".jpg") || endsWith(path, ".jpeg") || endsWith(path, ".jpg.gz") ||
        endsWith(path, ".jpeg.gz"))
        return "image/jpeg";
    if (endsWith(path, ".ico") || endsWith(path, ".ico.gz")) return "image/x-icon";
    if (endsWith(path, ".svg") || endsWith(path, ".svg.gz")) return "image/svg+xml";
    return "text/plain";
}

void serveStaticFile(HttpRequest &req) {
    if (req.method != HttpMethod::GET) {
        req.send(405, "text/plain", "Method Not Allowed");
        return;
    }

    std::string path = req.path;
    if (path.empty() || path[0] != '/') {
        path = "/" + path;
    }
    if (path == "/") {
        path = "/index.html";
    }
    path = "public" + path;
    const std::string gzPath = path + ".gz";

    bool gzip = false;
    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        req.send(408, "text/plain", "Request Timeout");
        return;
    }
    if (sd.exists(gzPath.c_str())) {
        gzip = true;
        path = gzPath;
    } else if (!sd.exists(path.c_str())) {
        mutex_exit(&sdMu);
        req.send(404, "text/plain", "Not Found");
        return;
    }
    mutex_exit(&sdMu);

    auto *job = FileStreamJob::open(req, path,
                                    {.contentType = contentTypeForPath(path),
                                     .extraHeaders = gzip ? "Content-Encoding: gzip\r\n" : nullptr});
    if (job) {
        c0Queue.add(&FileStreamJob::stepTask, 5, job);
    }
}

// ---- message-log truncation (drops everything before the last restart
// marker, keeping the current boot's log) ----

void truncateMessageLog(HttpRequest &req) {
    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        req.send(408, "text/plain", "Request Timeout");
        return;
    }
    if (!sd.exists(MESSAGE_LOG_PATH)) {
        mutex_exit(&sdMu);
        req.send(404, "text/plain", "not found");
        return;
    }
    FsFile src = sd.open(MESSAGE_LOG_PATH, O_READ);
    if (!src) {
        mutex_exit(&sdMu);
        req.send(500, "text/plain", "could not open log");
        return;
    }

    const size_t fileSize = src.size();

    // Scan backward in chunks for the last restart marker.
    static char window[1024 + 16];
    size_t chunkEnd = fileSize;
    uint32_t startOffset = 0;
    bool markerFound = false;
    uint32_t lastMarkerOffset = 0;
    size_t overlapLen = 0;
    char overlap[16];

    while (chunkEnd > 0 && !markerFound) {
        const uint32_t chunkStart = (chunkEnd > 1024) ? (chunkEnd - 1024) : 0;
        const size_t chunkLen = chunkEnd - chunkStart;
        if (!src.seek(chunkStart)) {
            src.close();
            mutex_exit(&sdMu);
            req.send(500, "text/plain", "could not seek log");
            return;
        }
        const int readLen = src.read(window, chunkLen);
        if (readLen < 0 || static_cast<size_t>(readLen) != chunkLen) {
            src.close();
            mutex_exit(&sdMu);
            req.send(500, "text/plain", "could not read log");
            return;
        }
        if (overlapLen > 0) {
            memcpy(window + chunkLen, overlap, overlapLen);
        }
        const size_t totalLen = chunkLen + overlapLen;
        const size_t markerLen = strlen(kRestartMarker);
        if (totalLen >= markerLen) {
            for (size_t i = totalLen - markerLen + 1; i > 0; i--) {
                const size_t idx = i - 1;
                if (memcmp(window + idx, kRestartMarker, markerLen) == 0) {
                    lastMarkerOffset = chunkStart + idx;
                    markerFound = true;
                    break;
                }
            }
        }
        overlapLen = min(chunkLen, markerLen - 1);
        if (overlapLen > 0) {
            memcpy(overlap, window, overlapLen);
        }
        chunkEnd = chunkStart;
    }
    startOffset = markerFound ? lastMarkerOffset : 0;

    if (!src.seek(startOffset)) {
        src.close();
        mutex_exit(&sdMu);
        req.send(500, "text/plain", "could not seek log");
        return;
    }
    FsFile tmp = sd.open(MESSAGE_LOG_PATH ".trunc", O_WRITE | O_CREAT | O_TRUNC);
    if (!tmp) {
        src.close();
        mutex_exit(&sdMu);
        req.send(500, "text/plain", "could not open temp log");
        return;
    }

    char copyBuf[512];
    int readLen;
    bool writeFailed = false;
    while ((readLen = src.read(copyBuf, sizeof(copyBuf))) > 0) {
        if (tmp.write(copyBuf, static_cast<size_t>(readLen)) != static_cast<size_t>(readLen)) {
            writeFailed = true;
            break;
        }
    }
    tmp.flush();
    tmp.close();
    src.close();

    if (writeFailed) {
        sd.remove(MESSAGE_LOG_PATH ".trunc");
        mutex_exit(&sdMu);
        req.send(500, "text/plain", "could not write temp log");
        return;
    }

    sd.remove(MESSAGE_LOG_PATH);
    const bool renamed = sd.rename(MESSAGE_LOG_PATH ".trunc", MESSAGE_LOG_PATH);
    if (!renamed) {
        sd.remove(MESSAGE_LOG_PATH ".trunc");
    }
    mutex_exit(&sdMu);

    if (!renamed) {
        req.send(500, "text/plain", "could not replace log");
        return;
    }
    req.send(204, "text/plain", "");
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
    router.on(HttpMethod::GET, "/readyz", [](HttpRequest &req) { req.send(200, contentTypePlain, ""); });
    router.on(HttpMethod::GET, "/livez", [](HttpRequest &req) { req.send(200, contentTypePlain, ""); });

    router.onUpload(HttpMethod::POST, "/ota", [](HttpRequest &) {
        return std::make_unique<OtaUploadHandler>();
    });
    router.onUpload(HttpMethod::POST, "/ota/public", [](HttpRequest &) {
        return std::make_unique<PublicUploadHandler>();
    });

    // Serve "public/" from the SD card for anything else.
    router.onNotFound(serveStaticFile);
}
