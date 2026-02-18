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
void handleNotFound();
void handleOtaFinish();
void handleOtaUpload();
void handlePublicUploadFinish();
void handlePublicUpload();
void handleDeviceAction();
void handleMetrics();

void setupAPI() {
    server.on("/config", HTTP_GET, handleGetConfig);
    server.on("/config", HTTP_POST, handlePostConfig);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/energy", HTTP_GET, handleEnergy);
    server.on("/device/action", HTTP_POST, handleDeviceAction);
    server.on("/logs", HTTP_GET, handleLogs);
    server.on("/ota", HTTP_POST, handleOtaFinish, handleOtaUpload);
    server.on("/ota/public", HTTP_POST, handlePublicUploadFinish, handlePublicUpload);
    server.on("/metrics", HTTP_GET, handleMetrics);
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
        msg.concat(err->Error());
        msg.concat("\"}");
        server.send(400, contentTypeJSON, msg);
        return;
    }

    err = saveConfig();
    if (err) {
        returnInternalError(err->Error());
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
    deviceActionType action = deviceActionType::None;

    if (strcmp(actionStr, "locate") == 0) {
        action = deviceActionType::Locate;
    } else if (strcmp(actionStr, "assign") == 0) {
        action = deviceActionType::Assign;
    } else {
        server.send(400, contentTypeJSON, F("{\"error\":\"Unknown action\"}"));
        return;
    }

    if (address == 0 || address > MAX_DEVICES) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid address\"}"));
        return;
    }

    if (!mutex_enter_block_until(&deviceActionMu, 100)) {
        returnInternalError("could not acquire deviceInfoMu");
        return;
    }

    if (deviceActionControl.type != deviceActionType::None) {
        mutex_exit(&deviceActionMu);
        server.send(409, contentTypeJSON, F("{\"error\":\"Action already pending\"}"));
        return;
    }

    deviceActionControl = {action, static_cast<uint8_t>(address)};

    mutex_exit(&deviceActionMu);

    server.send(202, contentTypeJSON, F("{\"status\":\"queued\"}"));
}

void handleMetrics() {
    const uint32_t errors = metrics.modbus_errors_total.load(std::memory_order_relaxed);
    const uint64_t totalMs = metrics.modbus_collect_time_ms_total.load(std::memory_order_relaxed);
    const uint32_t avgMs = metrics.modbus_last_run_avg_ms.load(std::memory_order_relaxed);

    String response;
    response.reserve(320);
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
    response += F(
        "# HELP auramon_collect_time_seconds_avg Average per-device collection time for the last run in seconds.\n");
    response += F("# TYPE auramon_collect_time_seconds_avg gauge\n");
    response += F("auramon_collect_time_seconds_avg ");
    response += String(avgMs / 1000.0, 6);
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
        if (!data || !data->name || !data->name[0]) {
            continue;
        }

        auto deviceObj = devicesArr.add<JsonObject>();
        deviceObj["name"] = String(data->name);
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

    JsonObject networkObj = doc["network"].to<JsonObject>();
    networkObj["hostname"] = netCfg.hostname;
    networkObj["ip"] = eth.localIP().toString();
    networkObj["gateway"] = eth.gatewayIP().toString();
    networkObj["subnet"] = eth.subnetMask().toString();
    networkObj["dns"] = eth.dnsIP().toString();
    char mac_str[18];
    sprintf_P(mac_str, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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
    if (end > start + interval * 100) {
        // Limit to 100 rows to prevent excessively large responses.
        end = start + interval * 100;
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
        if (!info || !info->isEnabled() || !info->name || !info->name[0]) {
            continue;
        }
        deviceColumns[deviceCount++] = deviceColumn{i, String(info->name)};
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

    logRecord prevRec;
    if (auto err = datalog.read(start - interval, &prevRec); err) {
        returnInternalError(err->Error());
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
        logRecord rec;
        if (auto err = datalog.read(ts, &rec); err) {
            server.sendContent(F("#error reading datalog\n"));
            server.chunkedResponseFinalize();
            return;
        }

        if (rec.rev == prevRec.rev) {
            continue;
        }

        const double elapsedHours = rec.logHours - prevRec.logHours;
        if (elapsedHours <= 0) {
            prevRec = rec;
            continue;
        }

        auto row = String(ts);
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
        limitBytes = server.arg("limitBytes").toInt();
        if (limitBytes == 0) {
            server.send(400, contentTypeJSON, F("{\"error\":\"Invalid limit\"}"));
            return;
        }
    }

    if (!mutex_enter_block_until(&sdMu, 100)) {
        server.send(408, contentTypePlain, "Request Timeout");
        return;
    }

    if (!sd.exists(MESSAGE_LOG_PATH)) {
        mutex_exit(&sdMu);
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
    }

    if (auto f = sd.open(MESSAGE_LOG_PATH, O_READ); f) {
        const size_t fileSize = f.size();
        if (startOffset >= fileSize) {
            server.send(204, contentTypePlain, "");
            f.close();
            mutex_exit(&sdMu);
            return;
        }
        if (startOffset > 0 && !f.seek(startOffset)) {
            returnInternalError("could not seek log");
            f.close();
            mutex_exit(&sdMu);
            return;
        }

        size_t remaining = fileSize - startOffset;
        if (limitBytes > 0 && limitBytes < remaining) {
            remaining = limitBytes;
        }
        if (remaining == 0) {
            server.send(204, contentTypePlain, "");
            f.close();
            mutex_exit(&sdMu);
            return;
        }

        if (!server.chunkedResponseModeStart(200, contentTypePlain)) {
            server.send(505, contentTypePlain, F("HTTP1.1 required"));
            f.close();
            mutex_exit(&sdMu);
            return;
        }

        uint8_t buffer[1024];
        while (remaining > 0) {
            const size_t readLen = f.read(buffer, min(remaining, sizeof(buffer)));
            if (readLen == 0) {
                break;
            }
            server.sendContent(reinterpret_cast<char *>(buffer), readLen);
            remaining -= readLen;
        }
        server.chunkedResponseFinalize();
        f.close();

        mutex_exit(&sdMu);
        return;
    }
    mutex_exit(&sdMu);

    server.send(404, contentTypeJSON, "Not Found");
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

            mutex_enter_blocking(&sdMu);
            delay(100);
            rp2040.reboot();
        }

        LOGE("OTA: update failed with code %u", otaErrorCode);

        return;
    }

    LOGI("OTA: update finished, rebooting");

    server.send(204, contentTypePlain, "");
    mutex_enter_blocking(&sdMu);
    delay(100);
    rp2040.reboot();
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

static bool   publicUploadFailed = false;
static int    publicUploadStatus = 200;
static String publicUploadError;
static bool   publicUploadMutexHeld = false;
static FsFile publicUploadFile;

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

void handlePublicUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        publicUploadFailed = false;
        publicUploadStatus = 200;
        publicUploadError = "";

        if (upload.name != "file") {
            publicUploadFailed = true;
            publicUploadStatus = 400;
            publicUploadError = F("Unexpected form field name");
            LOGE("Public upload: unexpected form field name: %s", upload.name.c_str());
            return;
        }

        if (upload.filename.length() == 0 || upload.filename.indexOf('/') >= 0 ||
            upload.filename.indexOf('\\') >= 0) {
            publicUploadFailed = true;
            publicUploadStatus = 400;
            publicUploadError = F("Invalid filename");
            LOGE("Public upload: invalid filename: %s", upload.filename.c_str());
            return;
        }

        String path = "public/";
        path.concat(upload.filename);

        LOGI("Public upload: start %s", path.c_str());

        if (!mutex_enter_block_until(&sdMu, 100)) {
            publicUploadFailed = true;
            publicUploadStatus = 408;
            publicUploadError = F("Request Timeout");
            LOGE("Public upload: failed to acquire sdMu");
            return;
        }
        publicUploadMutexHeld = true;

        publicUploadFile = sd.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
        if (!publicUploadFile) {
            publicUploadFailed = true;
            publicUploadStatus = 500;
            publicUploadError = F("Failed to open file");
            mutex_exit(&sdMu);
            LOGE("Public upload: failed to open %s", path.c_str());
            publicUploadMutexHeld = false;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE && !publicUploadFailed) {
        if (publicUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
            publicUploadFailed = true;
            publicUploadStatus = 500;
            publicUploadError = F("Write failed");
            Serial.printf("Public upload: write failed at %u bytes", upload.totalSize);
            publicUploadFile.close();
            if (publicUploadMutexHeld) {
                mutex_exit(&sdMu);
                publicUploadMutexHeld = false;
            }
        }
    } else if (upload.status == UPLOAD_FILE_END && !publicUploadFailed) {
        publicUploadFile.flush();
        publicUploadFile.close();

        if (publicUploadMutexHeld) {
            mutex_exit(&sdMu);
            publicUploadMutexHeld = false;
        }

        LOGI("Public upload: complete (%u bytes)", upload.totalSize);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        publicUploadFailed = true;
        publicUploadStatus = 500;
        publicUploadError = F("Upload aborted");

        if (publicUploadFile) {
            publicUploadFile.close();
        }

        if (publicUploadMutexHeld) {
            mutex_exit(&sdMu);
            publicUploadMutexHeld = false;
        }

        LOGE("Public upload: aborted");
    }
}

void handleNotFound() {
    LOGD("NotFound requested URL: %s", server.uri().c_str());

    if (server.method() != HTTP_GET) {
        server.send(405, contentTypePlain, "Method Not Allowed");
        return;
    }

    if (!mutex_enter_block_until(&sdMu, 100)) {
        server.send(408, contentTypePlain, "Request Timeout");
        return;
    }

    String path = server.uri();
    if (!path.startsWith("/")) path = '/' + path;
    if (path == "/") path = "/index.html";
    path = "public" + path;
    auto gzPath = path + ".gz";

    if (sd.exists(gzPath.c_str())) {
        server.sendHeader("Content-Encoding", "gzip");
        path = gzPath;
    } else if (!sd.exists(path.c_str())) {
        mutex_exit(&sdMu);
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
    }

    if (auto f = sd.open(path.c_str(), O_READ); f) {
        if (f.isDirectory()) {
            f.close();
            mutex_exit(&sdMu);
            server.send(403, contentTypePlain, "Forbidden");
            return;
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
            f.close();
            mutex_exit(&sdMu);
            return;
        }

        uint8_t buffer[1024];
        while (size_t readLen = f.read(buffer, sizeof(buffer))) {
            server.sendContent(reinterpret_cast<char *>(buffer), readLen);
        }
        server.chunkedResponseFinalize();
        f.close();

        mutex_exit(&sdMu);
        return;
    }
    mutex_exit(&sdMu);

    server.send(404, contentTypeJSON, "Not Found");
}
