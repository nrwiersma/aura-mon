//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

#include <Updater.h>
#include <LittleFS.h>

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";
const char *contentTypeHTML PROGMEM = "text/html";
const char *contentTypeCSV PROGMEM = "text/csv";

void returnOK();
void handleGetConfig();
void handlePostConfig();
void handleStatus();
void handleEnergy();
void handleLogs();
void handleLogsTrunc();
void handleNotFound();
void handleOtaFinish();
void handleOtaUpload();
void handlePublicUploadFinish();
void handlePublicUpload();
void handleDeviceAction();
void handleMetrics();
void handleReboot();

void setupAPI() {
    server.on("/config", HTTP_GET, handleGetConfig);
    server.on("/config", HTTP_POST, handlePostConfig);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/energy", HTTP_GET, handleEnergy);
    server.on("/device/action", HTTP_POST, handleDeviceAction);
    server.on("/logs", HTTP_GET, handleLogs);
    server.on("/logs/trunc", HTTP_POST, handleLogsTrunc);
    server.on("/ota", HTTP_POST, handleOtaFinish, handleOtaUpload);
    server.on("/ota/public", HTTP_POST, handlePublicUploadFinish, handlePublicUpload);
    server.on("/metrics", HTTP_GET, handleMetrics);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.on("/readyz", HTTP_GET, returnOK);
    server.on("/livez", HTTP_GET, returnOK);

    server.onNotFound(handleNotFound); // Serve "public" from SD Card.
    server.enableCORS(true);
    server.enableCrossOrigin(true);
}

void returnOK() {
    server.send(200, contentTypePlain, "");
}

void returnInternalError(const char *reason) {
    String msg = "{\"error\":\"Internal Error\",\"reason\":\"";
    msg.concat(reason);
    msg.concat("\"}");
    server.send(500, contentTypeJSON, msg);
}

// Releasing a pin must not be skipped on a busy card: there are only a handful
// of slots and a leaked one is never reclaimed, so this waits longer than a
// normal transaction would.
void unpinPath(const char *path) {
    if (auto err = storage::run([&](storage::sdAccess &sd) {
        sd.unpin(path);
        return error{};
    }, SD_LOCK_TIMEOUT_MS * 4); err) {
        LOGE("Could not release %s: %s", path, err.Error());
    }
}

// Streams len bytes of path from pos as chunked content. The card lock is taken
// per chunk and released while the chunk is sent, so a slow client cannot hold
// the card, and queued work is drained between chunks.
void sendFileChunked(const char *path, uint32_t pos, uint32_t len) {
    uint8_t buffer[1024];

    while (len > 0) {
        const auto want = static_cast<uint32_t>(min(static_cast<size_t>(len), sizeof(buffer)));

        if (auto err = storage::run([&](storage::sdAccess &sd) {
            return sd.readAt(path, pos, buffer, want);
        }); err) {
            break;
        }

        server.sendContent(reinterpret_cast<char *>(buffer), want);
        pos += want;
        len -= want;

        storage::pump();
    }
}

struct deviceColumn {
    uint8_t index;
    String  name;
};

void appendCSVValue(String &row, double value, const uint8_t precision = 3) {
    row += ",";
    if (std::isfinite(value)) {
        row += String(value, precision);
    }
}

void handleGetConfig() {
    JsonDocument doc;
    saveConfigJSON(doc);

    String response;
    serializeJson(doc, response);

    server.send(200, contentTypeJSON, response);
}

void handlePostConfig() {
    if (server.hasArg("plain") == false) {
        server.send(400, contentTypeJSON, F("{\"error\":\"No data provided\"}"));
        return;
    }

    String body = server.arg("plain");

    JsonDocument doc;
    if (auto err = deserializeJson(doc, body); err) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid JSON\"}"));
        return;
    }

    auto err = loadConfigJSON(doc);
    if (err) {
        String msg = "{\"error\":\"Invalid configuration\",\"reason\":\"";
        msg.concat(err.Error());
        msg.concat("\"}");
        server.send(400, contentTypeJSON, msg);
        return;
    }

    err = saveConfig();
    if (err) {
        returnInternalError(err.Error());
        return;
    }

    mutex_enter_blocking(&deviceInfoMu);
    devicesChanged = true;
    mutex_exit(&deviceInfoMu);

    server.send(200, contentTypePlain, "");
}

void handleDeviceAction() {
    if (server.hasArg("plain") == false) {
        server.send(400, contentTypeJSON, F("{\"error\":\"No data provided\"}"));
        return;
    }

    String body = server.arg("plain");

    JsonDocument doc;
    if (auto err = deserializeJson(doc, body); err) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid JSON\"}"));
        return;
    }

    if (!doc["action"].is<const char *>() || !doc["address"].is<uint32_t>()) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid action payload\"}"));
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
        server.send(400, contentTypeJSON, F("{\"error\":\"Unknown action\"}"));
        return;
    }

    if (address == 0 || address > MAX_DEVICES) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid address\"}"));
        return;
    }

    if (!mutex_enter_timeout_ms(&deviceActionMu, 100)) {
        returnInternalError("could not acquire deviceInfoMu");
        return;
    }

    if (deviceActionControl.type != DeviceActionType::None) {
        mutex_exit(&deviceActionMu);
        server.send(409, contentTypeJSON, F("{\"error\":\"Action already pending\"}"));
        return;
    }

    deviceActionControl = {action, static_cast<uint8_t>(address)};

    mutex_exit(&deviceActionMu);

    server.send(202, contentTypeJSON, F("{\"status\":\"queued\"}"));
}

void handleReboot() {
    LOGI("Reboot requested");

    server.send(204, contentTypePlain, "");

    safeReboot();
}

void handleMetrics() {
    const uint32_t errors = metrics.modbus_errors_total.load(std::memory_order_relaxed);
    const uint64_t totalMs = metrics.modbus_collect_time_ms_total.load(std::memory_order_relaxed);
    const uint32_t avgMs = metrics.modbus_last_run_avg_ms.load(std::memory_order_relaxed);
    const uint32_t collectRuns = metrics.modbus_collect_runs_total.load(std::memory_order_relaxed);
    const uint32_t datalogReadIO = metrics.datalog_read_io.load(std::memory_order_relaxed);
    const uint32_t datalogWriteIO = metrics.datalog_write_io.load(std::memory_order_relaxed);
    const uint64_t datalogWriteUsTotal = metrics.datalog_write_time_us_total.load(std::memory_order_relaxed);
    const uint32_t datalogWriteUsMax = metrics.datalog_write_time_us_max.load(std::memory_order_relaxed);
    const uint32_t sdQueueDepth = metrics.sd_queue_depth.load(std::memory_order_relaxed);
    const uint32_t sdQueueHighWater = metrics.sd_queue_high_water.load(std::memory_order_relaxed);
    const uint32_t sdQueueDropped = metrics.sd_queue_dropped_total.load(std::memory_order_relaxed);
    const uint32_t sdQueueLatencyUsMax = metrics.sd_queue_latency_us_max.load(std::memory_order_relaxed);
    const uint32_t sdJobErrors = metrics.sd_job_errors_total.load(std::memory_order_relaxed);
    const uint32_t datalogCacheHit = metrics.datalog_cache_hit.load(std::memory_order_relaxed);
    const uint32_t datalogWriteErrors = metrics.datalog_write_errors_total.load(std::memory_order_relaxed);
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
    response += String(datalogWriteUsTotal / 1000000.0, 6);
    response += '\n';
    response += F("# HELP auramon_datalog_write_time_seconds_max Slowest single datalog write in seconds since boot.\n");
    response += F("# TYPE auramon_datalog_write_time_seconds_max gauge\n");
    response += F("auramon_datalog_write_time_seconds_max ");
    response += String(datalogWriteUsMax / 1000000.0, 6);
    response += '\n';
    response += F("# HELP auramon_sd_queue_depth Number of jobs currently waiting in the SD write queue.\n");
    response += F("# TYPE auramon_sd_queue_depth gauge\n");
    response += F("auramon_sd_queue_depth ");
    response += String(sdQueueDepth);
    response += '\n';
    response += F("# HELP auramon_sd_queue_high_water Deepest the SD write queue has been since boot.\n");
    response += F("# TYPE auramon_sd_queue_high_water gauge\n");
    response += F("auramon_sd_queue_high_water ");
    response += String(sdQueueHighWater);
    response += '\n';
    response += F("# HELP auramon_sd_queue_dropped_total Total jobs dropped because the SD write queue was full.\n");
    response += F("# TYPE auramon_sd_queue_dropped_total counter\n");
    response += F("auramon_sd_queue_dropped_total ");
    response += String(sdQueueDropped);
    response += '\n';
    response += F("# HELP auramon_sd_queue_latency_seconds_max Longest a job has waited in the SD queue before running.\n");
    response += F("# TYPE auramon_sd_queue_latency_seconds_max gauge\n");
    response += F("auramon_sd_queue_latency_seconds_max ");
    response += String(static_cast<double>(sdQueueLatencyUsMax) / 1000000.0, 6);
    response += '\n';
    response += F("# HELP auramon_sd_job_errors_total Total queued SD jobs that returned an error.\n");
    response += F("# TYPE auramon_sd_job_errors_total counter\n");
    response += F("auramon_sd_job_errors_total ");
    response += String(sdJobErrors);
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

    server.send(200, contentTypePlain, response);
}

void handleStatus() {
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

    server.send(200, contentTypeJSON, response);
}

void handleEnergy() {
    uint32_t baseInterval = datalog.interval();
    uint32_t start = server.arg("start").toInt();
    uint32_t end = server.hasArg("end") ? server.arg("end").toInt() : time(nullptr);
    uint32_t interval = server.hasArg("interval") ? server.arg("interval").toInt() : 5;

    LOGD("Energy request start=%u end=%u interval=%u", start, end, interval);

    start -= start % baseInterval;
    end -= end % baseInterval;
    interval -= interval % baseInterval;

    if (start >= end || interval == 0) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid parameters\"}"));
        return;
    }
    if (end > start + interval * 99) {
        // Limit to 100 rows to prevent excessively large responses.
        end = start + interval * 99;
    }

    if (!datalog.entries()) {
        server.send(204, contentTypePlain, "");
        return;
    }

    LOGD("energy: adjusted parameters start=%u end=%u interval=%u", start, end, interval);

    deviceColumn deviceColumns[MAX_DEVICES];
    size_t       deviceCount = 0;
    mutex_enter_blocking(&deviceInfoMu);
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        auto info = deviceInfos[i];
        if (!info || !info->isEnabled() || info->name.isEmpty()) {
            continue;
        }
        deviceColumns[deviceCount++] = deviceColumn{i, info->name};
    }
    mutex_exit(&deviceInfoMu);

    LOGD("energy: collected devices: %u", deviceCount);

    if (deviceCount == 0) {
        server.send(204, contentTypePlain, "");
        return;
    }

    uint32_t lastTs = datalog.lastTS();
    if (start > lastTs) {
        server.send(204, contentTypePlain, "");
        return;
    }
    if (end > lastTs) {
        end = lastTs;
    }

    LogRecord prevRec;
    if (auto err = datalog.read(start - interval, &prevRec); err) {
        returnInternalError(err.Error());
        return;
    }

    LOGD("energy: read previous record: %u", prevRec.rev);

    if (!server.chunkedResponseModeStart(200, contentTypePlain)) {
        server.send(505, contentTypeHTML, F("HTTP1.1 required"));
        return;
    }

    String header = F("timestamp,Hz");
    for (size_t i = 0; i < deviceCount; i++) {
        const String &name = deviceColumns[i].name;
        header += "," + name + ".V";
        header += "," + name + ".A";
        header += "," + name + ".W";
        header += "," + name + ".Wh";
        header += "," + name + ".PF";
    }
    header += "\n";
    server.sendContent(header);

    for (uint32_t ts = start; ts <= end; ts += interval) {
        LogRecord rec;
        if (auto err = datalog.read(ts, &rec); err) {
            server.sendContent(F("error reading datalog\n"));
            server.chunkedResponseFinalize();
            return;
        }

        if (rec.ts <= prevRec.ts) {
            continue;
        }
        if (rec.rev == prevRec.rev) {
            continue;
        }

        const double elapsedHours = rec.logHours - prevRec.logHours;
        if (elapsedHours <= 0) {
            prevRec = rec;
            continue;
        }

        auto row = String(rec.ts);
        row.reserve(row.length() + deviceCount * 48);

        const double hz = (rec.hzHrs - prevRec.hzHrs) / elapsedHours;
        appendCSVValue(row, hz, 2);

        for (size_t i = 0; i < deviceCount; i++) {
            const uint8_t idx = deviceColumns[i].index;
            const double  voltage = (rec.voltHrs[idx] - prevRec.voltHrs[idx]) / elapsedHours;
            double        energyWh = rec.wattHrs[idx] - prevRec.wattHrs[idx];
            const double  power = energyWh / elapsedHours;
            const double  apparentPower = (rec.vaHrs[idx] - prevRec.vaHrs[idx]) / elapsedHours;
            if (energyWh < 0) {
                energyWh = 0;
            }
            const double current = (voltage != 0.0) ? (apparentPower / voltage) : 0.0;
            const double powerFactor = (apparentPower > 0.0) ? (power / apparentPower) : 0.0;

            appendCSVValue(row, voltage);
            appendCSVValue(row, current);
            appendCSVValue(row, power);
            appendCSVValue(row, energyWh, 6);
            appendCSVValue(row, powerFactor, 4);
        }

        row += "\n";
        server.sendContent(row);
        prevRec = rec;
    }

    LOGD("energy: completed response");

    server.chunkedResponseFinalize();
}

void handleLogs() {
    uint32_t startOffset = 0;
    if (server.hasArg("start")) {
        startOffset = server.arg("start").toInt();
        if (startOffset == 0 && server.arg("start") != "0") {
            server.send(400, contentTypeJSON, F("{\"error\":\"Invalid start\"}"));
            return;
        }
    }
    uint32_t limitBytes = 0;
    if (server.hasArg("limit")) {
        limitBytes = server.arg("limit").toInt();
        if (limitBytes == 0) {
            server.send(400, contentTypeJSON, F("{\"error\":\"Invalid limit\"}"));
            return;
        }
    }

    bool     found = false;
    uint32_t fileSize = 0;

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (!sd.exists(MESSAGE_LOG_PATH)) {
            return {};
        }
        found = true;
        fileSize = sd.size(MESSAGE_LOG_PATH);

        // Held open for the duration of the transfer so each chunk does not
        // reopen the file. The card lock is still released between chunks.
        sd.pin(MESSAGE_LOG_PATH);
        return {};
    });
    if (err) {
        server.send(408, contentTypePlain, "Request Timeout");
        return;
    }
    if (!found) {
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }

    uint32_t remaining = startOffset >= fileSize ? 0 : fileSize - startOffset;
    if (limitBytes > 0 && limitBytes < remaining) {
        remaining = limitBytes;
    }
    if (remaining == 0) {
        unpinPath(MESSAGE_LOG_PATH);
        server.send(204, contentTypePlain, "");
        return;
    }

    if (!server.chunkedResponseModeStart(200, contentTypePlain)) {
        unpinPath(MESSAGE_LOG_PATH);
        server.send(505, contentTypePlain, F("HTTP1.1 required"));
        return;
    }

    sendFileChunked(MESSAGE_LOG_PATH, startOffset, remaining);

    server.chunkedResponseFinalize();
    unpinPath(MESSAGE_LOG_PATH);
}

constexpr char   restartMarker[] = "**** RESTART ****";
constexpr size_t restartMarkerLen = sizeof(restartMarker) - 1;
constexpr char   messageLogTmpPath[] = MESSAGE_LOG_PATH ".trunc";

// Rewrites the message log to contain only what follows the last restart
// marker, reporting the offset it was cut at.
error truncateLog(storage::sdAccess &sd, uint32_t &startOffset) {
    const uint32_t fileSize = sd.size(MESSAGE_LOG_PATH);
    if (fileSize == 0) {
        startOffset = 0;
        return {};
    }

    constexpr size_t chunkSize = 1024;
    uint8_t          window[chunkSize + restartMarkerLen - 1];
    uint8_t          overlap[restartMarkerLen - 1];
    size_t           overlapLen = 0;
    uint32_t         lastMarkerOffset = 0;
    bool             markerFound = false;

    for (uint32_t chunkEnd = fileSize; chunkEnd > 0 && !markerFound;) {
        const uint32_t chunkStart = (chunkEnd > chunkSize) ? (chunkEnd - chunkSize) : 0;
        const size_t   chunkLen = chunkEnd - chunkStart;

        if (auto e = sd.readAt(MESSAGE_LOG_PATH, chunkStart, window, chunkLen); e) {
            return e;
        }
        if (overlapLen > 0) {
            // Copy the overlap to the end of the window.
            memcpy(window + chunkLen, overlap, overlapLen);
        }

        const size_t totalLen = chunkLen + overlapLen;
        if (totalLen >= restartMarkerLen) {
            for (size_t i = totalLen - restartMarkerLen + 1; i > 0; i--) {
                const size_t idx = i - 1;
                if (memcmp(window + idx, restartMarker, restartMarkerLen) == 0) {
                    lastMarkerOffset = chunkStart + idx;
                    markerFound = true;
                    break;
                }
            }
        }

        // Copy the start of the chunk into overlap, to be checked in the next round
        // to find markers over the boundary.
        overlapLen = min(chunkLen, restartMarkerLen - 1);
        if (overlapLen > 0) {
            memcpy(overlap, window, overlapLen);
        }

        chunkEnd = chunkStart;
    }

    startOffset = markerFound ? lastMarkerOffset : 0;
    if (startOffset == 0) {
        // Nothing to trim, so leave the log alone rather than rewriting it.
        return {};
    }

    // Removed before pinning: remove() drops the pin, so pinning first would
    // reopen the file on every chunk of the copy.
    sd.remove(messageLogTmpPath);
    sd.pin(messageLogTmpPath);

    uint8_t  buffer[chunkSize];
    uint32_t pos = startOffset;
    while (pos < fileSize) {
        const auto want = static_cast<uint32_t>(min(static_cast<size_t>(fileSize - pos), sizeof(buffer)));

        if (auto e = sd.readAt(MESSAGE_LOG_PATH, pos, buffer, want); e) {
            sd.unpin(messageLogTmpPath);
            sd.remove(messageLogTmpPath);
            return e;
        }
        if (auto e = sd.append(messageLogTmpPath, buffer, want); e) {
            sd.unpin(messageLogTmpPath);
            sd.remove(messageLogTmpPath);
            return e;
        }
        pos += want;
    }

    sd.unpin(messageLogTmpPath);

    if (auto e = sd.remove(MESSAGE_LOG_PATH); e) {
        sd.remove(messageLogTmpPath);
        return e;
    }
    // From here the temp file is the only copy of the log, so a failure must
    // leave it in place rather than remove it.
    return sd.rename(messageLogTmpPath, MESSAGE_LOG_PATH);
}

void handleLogsTrunc() {
    LOGI("Log truncation requested");

    bool     found = false;
    uint32_t startOffset = 0;

    // Unlike the log download, this runs as a single transaction: there is no
    // network I/O in the copy, so it is bounded by card throughput rather than
    // by the client, and draining mid-copy would append lines to the source
    // after the point the replacement was taken from.
    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (!sd.exists(MESSAGE_LOG_PATH)) {
            return {};
        }
        found = true;

        sd.pin(MESSAGE_LOG_PATH);

        auto e = truncateLog(sd, startOffset);

        sd.unpin(MESSAGE_LOG_PATH);
        return e;
    });

    if (!found) {
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }
    if (err) {
        returnInternalError(err.Error());
        return;
    }

    LOGD("Log truncated at offset %u", startOffset);

    server.send(204, contentTypePlain, "");
}



static bool    otaRestartNeeded = false;
static bool    otaUploadFailed = false;
static uint8_t otaErrorCode = UPDATE_ERROR_OK;

void handleOtaFinish() {
    if (otaUploadFailed || Update.hasError()) {
        String msg = F("{\"error\":\"Update failed\",\"code\":");
        msg.concat(otaErrorCode);
        msg.concat("}");
        server.send(500, contentTypeJSON, msg);

        if (otaRestartNeeded) {
            LOGE("OTA: update failed with code %u. Rebooting", otaErrorCode);

            safeReboot();
        }

        LOGE("OTA: update failed with code %u", otaErrorCode);

        return;
    }

    LOGI("OTA: update finished, rebooting");

    server.send(204, contentTypePlain, "");
    safeReboot();
}

void handleOtaUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        otaUploadFailed = false;
        otaErrorCode = UPDATE_ERROR_OK;
        Update.clearError();

        if (upload.name != "firmware") {
            otaUploadFailed = true;
            otaErrorCode = UPDATE_ERROR_NO_DATA;
            LOGE("OTA: unexpected form field name: %s", upload.name.c_str());
            return;
        }

        FSInfo i;
        LittleFS.begin();
        LittleFS.info(i);
        uint32_t update_size = i.totalBytes - i.usedBytes;

        LOGI("OTA: start upload size=%u", update_size);

        if (!Update.begin(update_size)) {
            otaUploadFailed = true;
            otaErrorCode = Update.getError();
            LOGE("OTA: begin failed (%u)", otaErrorCode);
            return;
        }

        LOGD("OTA: update started");
    } else if (upload.status == UPLOAD_FILE_WRITE && !otaUploadFailed) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaUploadFailed = true;
            otaErrorCode = Update.getError();
            LOGE("OTA: write failed (%u)", otaErrorCode);
            return;
        }

        LOGD("OTA: written %u bytes", upload.totalSize);
    } else if (upload.status == UPLOAD_FILE_END && !otaUploadFailed) {
        if (!Update.end(true)) {
            otaUploadFailed = true;
            otaErrorCode = Update.getError();
            LOGE("OTA: end failed (%u)", otaErrorCode);
            return;
        }

        LOGI("OTA: upload complete (%u bytes)", upload.totalSize);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaUploadFailed = true;
        otaErrorCode = UPDATE_ERROR_STREAM;
        Update.end();

        LOGE("OTA: upload aborted\r");
    }
}

constexpr char uploadSuffix[] = ".upload";

static bool   publicUploadFailed = false;
static int    publicUploadStatus = 200;
static String publicUploadError;
static String publicUploadPath;
static String publicUploadTmpPath;

void handlePublicUploadFinish() {
    if (publicUploadFailed) {
        String msg = F("{\"error\":\"Upload failed\",\"reason\":\"");
        msg.concat(publicUploadError);
        msg.concat("\"}");
        server.send(publicUploadStatus, contentTypeJSON, msg);
        return;
    }

    server.send(204, contentTypePlain, "");
}

void failPublicUpload(const int status, const __FlashStringHelper *reason) {
    publicUploadFailed = true;
    publicUploadStatus = status;
    publicUploadError = reason;

    if (publicUploadTmpPath.length() > 0) {
        const String tmp = publicUploadTmpPath;
        if (auto err = storage::run([&](storage::sdAccess &sd) {
            sd.unpin(tmp.c_str());
            sd.remove(tmp.c_str());
            return error{};
        }, SD_LOCK_TIMEOUT_MS * 4); err) {
            LOGE("Could not discard %s: %s", tmp.c_str(), err.Error());
        }
        publicUploadTmpPath = "";
    }
}

// Uploads land in a temp file, one transaction per chunk, and are renamed into
// place only once complete. The card is therefore never held across the client's
// transfer, and an interrupted upload cannot leave a partial file served.
void handlePublicUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        publicUploadFailed = false;
        publicUploadStatus = 200;
        publicUploadError = "";
        publicUploadPath = "";
        publicUploadTmpPath = "";

        if (upload.name != "file") {
            failPublicUpload(400, F("Unexpected form field name"));
            LOGE("Public upload: unexpected form field name: %s", upload.name.c_str());
            return;
        }

        if (upload.filename.length() == 0 || upload.filename.indexOf('/') >= 0 ||
            upload.filename.indexOf('\\') >= 0) {
            failPublicUpload(400, F("Invalid filename"));
            LOGE("Public upload: invalid filename: %s", upload.filename.c_str());
            return;
        }

        publicUploadPath = "public/";
        publicUploadPath.concat(upload.filename);
        publicUploadTmpPath = publicUploadPath + uploadSuffix;

        LOGI("Public upload: start %s", publicUploadPath.c_str());

        if (auto err = storage::run([&](storage::sdAccess &sd) -> error {
            if (auto e = sd.mkdirFor(publicUploadTmpPath.c_str()); e) {
                return e;
            }
            sd.remove(publicUploadTmpPath.c_str());

            // Held open for the transfer so each chunk does not reopen the file.
            sd.pin(publicUploadTmpPath.c_str());
            return {};
        }); err) {
            const String tmp = publicUploadTmpPath;
            publicUploadTmpPath = "";
            failPublicUpload(500, F("Failed to open file"));
            LOGE("Public upload: failed to open %s: %s", tmp.c_str(), err.Error());
        }
        return;
    }

    if (publicUploadFailed) {
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (auto err = storage::run([&](storage::sdAccess &sd) {
            return sd.append(publicUploadTmpPath.c_str(), upload.buf, upload.currentSize);
        }); err) {
            LOGE("Public upload: write failed at %u bytes: %s", upload.totalSize, err.Error());
            failPublicUpload(500, F("Write failed"));
            return;
        }

        // Let queued work land between chunks rather than waiting out the
        // whole transfer.
        storage::pump();
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (auto err = storage::run([&](storage::sdAccess &sd) -> error {
            sd.unpin(publicUploadTmpPath.c_str());
            sd.remove(publicUploadPath.c_str());
            return sd.rename(publicUploadTmpPath.c_str(), publicUploadPath.c_str());
        }); err) {
            LOGE("Public upload: could not publish %s: %s", publicUploadPath.c_str(), err.Error());
            failPublicUpload(500, F("Write failed"));
            return;
        }

        publicUploadTmpPath = "";
        LOGI("Public upload: complete (%u bytes)", upload.totalSize);
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        failPublicUpload(500, F("Upload aborted"));
        LOGE("Public upload: aborted");
    }
}

void handleNotFound() {
    LOGD("NotFound requested URL: %s", server.uri().c_str());

    if (server.method() != HTTP_GET) {
        server.send(405, contentTypePlain, "Method Not Allowed");
        return;
    }

    String path = server.uri();
    if (!path.startsWith("/")) path = '/' + path;
    if (path == "/") path = "/index.html";
    if (path.endsWith(uploadSuffix)) {
        // An upload left behind by an interrupted transfer is not content.
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }
    path = "public" + path;
    auto gzPath = path + ".gz";

    bool     gzipped = false;
    bool     found = false;
    bool     directory = false;
    uint32_t fileSize = 0;

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (sd.exists(gzPath.c_str())) {
            gzipped = true;
        } else if (!sd.exists(path.c_str())) {
            return {};
        }

        const char *p = gzipped ? gzPath.c_str() : path.c_str();
        found = true;
        directory = sd.isDirectory(p);
        if (directory) {
            return {};
        }

        fileSize = sd.size(p);

        // Held open for the duration of the transfer so each chunk does not
        // reopen the file. The card lock is still released between chunks.
        sd.pin(p);
        return {};
    });
    if (err) {
        server.send(408, contentTypePlain, "Request Timeout");
        return;
    }
    if (!found) {
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }
    if (directory) {
        server.send(403, contentTypePlain, "Forbidden");
        return;
    }

    if (gzipped) {
        server.sendHeader("Content-Encoding", "gzip");
        path = gzPath;
    }

    String contentType = contentTypePlain;
    if (path.endsWith(".html") || path.endsWith(".html.gz")) {
        contentType = F("text/html");
    } else if (path.endsWith(".css") || path.endsWith(".css.gz")) {
        contentType = F("text/css");
    } else if (path.endsWith(".js") || path.endsWith(".js.gz")) {
        contentType = F("application/javascript");
    } else if (path.endsWith(".json") || path.endsWith(".json.gz")) {
        contentType = contentTypeJSON;
    } else if (path.endsWith(".png") || path.endsWith(".png.gz")) {
        contentType = F("image/png");
    } else if (path.endsWith(".jpg") || path.endsWith(".jpeg") ||
               path.endsWith(".jpg.gz") || path.endsWith(".jpeg.gz")) {
        contentType = F("image/jpeg");
    } else if (path.endsWith(".ico") || path.endsWith(".ico.gz")) {
        contentType = F("image/x-icon");
    } else if (path.endsWith(".svg") || path.endsWith(".svg.gz")) {
        contentType = F("image/svg+xml");
    }

    if (!server.chunkedResponseModeStart(200, contentType.c_str())) {
        server.send(505, contentTypePlain, F("HTTP1.1 required"));
        unpinPath(path.c_str());
        return;
    }

    sendFileChunked(path.c_str(), 0, fileSize);

    server.chunkedResponseFinalize();
    unpinPath(path.c_str());
}
