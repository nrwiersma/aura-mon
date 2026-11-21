//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

#include <Updater.h>
#include <LittleFS.h>
#include <HTTPRequest.h>
#include "csv_query.h"
#include "metrics_query.h"
#include "uploader/uploader.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";
const char *contentTypeHTML PROGMEM = "text/html";
const char *contentTypeCSV PROGMEM = "text/csv";

HTTPServer::ClientFuture hookEnergy(const String &method, const String &url, const String &params, WiFiClient *client,
                                    WebServer::ContentTypeFunction contentType);
HTTPServer::ClientFuture hookMetrics(const String &method, const String &url, const String &params, WiFiClient *client,
                                     WebServer::ContentTypeFunction contentType);

void returnOK();
void handleGetConfig();
void handlePostConfig();
void handleStatus();
void handleLogs();
void handleLogsTrunc();
void handleNotFound();
void handleOtaFinish();
void handleOtaUpload();
void handlePublicUploadFinish();
void handlePublicUpload();
void handleDeviceAction();
void handleReboot();

void setupAPI() {
    server.addHook(hookEnergy);
    server.addHook(hookMetrics);
    server.on("/config", HTTP_GET, handleGetConfig);
    server.on("/config", HTTP_POST, handlePostConfig);
    server.on("/status", HTTP_GET, handleStatus);
    // server.on("/energy", HTTP_GET, handleEnergy);
    server.on("/device/action", HTTP_POST, handleDeviceAction);
    server.on("/logs", HTTP_GET, handleLogs);
    server.on("/logs/trunc", HTTP_POST, handleLogsTrunc);
    server.on("/ota", HTTP_POST, handleOtaFinish, handleOtaUpload);
    server.on("/ota/public", HTTP_POST, handlePublicUploadFinish, handlePublicUpload);
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

HTTPServer::ClientFuture hookEnergy(const String &method, const String &url, const String &params, WiFiClient *client,
                                    WebServer::ContentTypeFunction contentType) {
    if (url != "/energy") {
        // This is not an energy request, pass it onto the handlers.
        return WebServer::CLIENT_REQUEST_CAN_CONTINUE;
    }
    if (method.equals("OPTIONS")) {
        // Handle CORS preflight.
        HTTPRequest req(method, url, params, client, contentType);
        req.enableCORS();
        req.send(204, contentTypePlain, "");
        req.finish();
        return WebServer::CLIENT_MUST_STOP;
    }
    if (!method.equals("GET")) {
        return WebServer::CLIENT_REQUEST_CAN_CONTINUE;
    }

    // From this point on, either CLIENT_MUST_STOP or CLIENT_IS_GIVEN must be returned.

    auto *req = new HTTPRequest(method, url, params, client, contentType);
    req->enableCORS();

    const long baseInterval = datalog.interval();
    const long requestedStart = req->hasArg("start") ? req->arg("start").toInt() : 0;
    const long requestedEnd = req->hasArg("end") ? req->arg("end").toInt() : time(nullptr);
    const long requestedInterval = req->hasArg("interval") ? req->arg("interval").toInt() : 5;
    if (requestedStart <= 0 || requestedEnd <= 0 || requestedInterval <= 0) {
        req->send(400, contentTypeJSON, F("{\"error\":\"Invalid parameters\"}"));
        req->finish();
        delete req;
        return WebServer::CLIENT_MUST_STOP;
    }

    uint32_t start = requestedStart;
    uint32_t end = requestedEnd;
    uint32_t interval = requestedInterval;

    start -= start % baseInterval;
    end -= end % baseInterval;
    interval -= interval % baseInterval;

    if (start >= end || interval == 0) {
        req->send(400, contentTypeJSON, F("{\"error\":\"Invalid parameters\"}"));
        req->finish();
        delete req;
        return WebServer::CLIENT_MUST_STOP;
    }
    if ((end - start) / interval > 99) {
        // Limit to 100 rows to prevent excessively large responses.
        end = start + interval * 99;
    }

    if (!datalog.entries()) {
        req->send(204, contentTypePlain, "");
        req->finish();
        delete req;
        return WebServer::CLIENT_MUST_STOP;
    }
    const uint32_t lastTs = datalog.lastTS();
    if (start > lastTs) {
        req->send(204, contentTypePlain, "");
        req->finish();
        delete req;
        return WebServer::CLIENT_MUST_STOP;
    }
    if (end > lastTs) {
        end = lastTs;
    }

    auto *qry = new csvQuery(start, end, interval, req);
    req->takeClientOwnership();
    queue.add(&csvQuery::stepTask, 4, qry);
    return WebServer::CLIENT_IS_GIVEN;
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

    if (deviceAction.type != DeviceActionType::None) {
        server.send(409, contentTypeJSON, F("{\"error\":\"Action already pending\"}"));
        return;
    }

    deviceAction = {action, static_cast<uint8_t>(address)};

    server.send(202, contentTypeJSON, F("{\"status\":\"queued\"}"));
}

void handleReboot() {
    LOGI("Reboot requested");

    server.send(204, contentTypePlain, "");

    safeReboot();
}

HTTPServer::ClientFuture hookMetrics(const String &method, const String &url, const String &params, WiFiClient *client,
                                     WebServer::ContentTypeFunction contentType) {
    if (url != "/metrics" || !method.equals("GET")) {
        // This is not a metrics scrape, pass it onto the handlers.
        return WebServer::CLIENT_REQUEST_CAN_CONTINUE;
    }

    // From this point on, either CLIENT_MUST_STOP or CLIENT_IS_GIVEN must be returned.

    if (metricsQuery::active()) {
        // Only one scrape is streamed at a time.
        HTTPRequest req(method, url, params, client, contentType);
        req.send(503, contentTypePlain, F("scrape already in progress\n"));
        req.finish();
        return WebServer::CLIENT_MUST_STOP;
    }

    auto *req = new HTTPRequest(method, url, params, client, contentType);
    req->enableCORS();
    auto *qry = new metricsQuery(req);
    req->takeClientOwnership();
    queue.add(&metricsQuery::stepTask, 4, qry);
    return WebServer::CLIENT_IS_GIVEN;
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

    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        auto data = devices[i];
        if (!data || data->name.isEmpty()) {
            continue;
        }

        auto deviceObj = devicesArr.add<JsonObject>();
        deviceObj["name"] = data->name;
        deviceObj["volts"] = data->current.volts;
        deviceObj["amps"] = data->current.volts != 0.0
                                ? (data->current.va / data->current.volts)
                                : 0.0;
        deviceObj["pf"] = data->current.va != 0.0 ? data->current.watts / data->current.va : 0.0;
        deviceObj["hz"] = data->current.hz;
    }

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
               activeMac[0], activeMac[1], activeMac[2], activeMac[3], activeMac[4], activeMac[5]);
    networkObj["mac"] = mac_str;

    JsonArray      uploadersArr = doc["uploaders"].to<JsonArray>();
    const uint32_t datalogLastTS = datalog.lastTS();
    forEachUploader([&](const Uploader &u) {
        auto uploaderObj = uploadersArr.add<JsonObject>();
        uploaderObj["id"] = u.id();
        uploaderObj["type"] = u.type();
        uploaderObj["state"] = u.stateName();
        uploaderObj["lastSentTS"] = u.lastSentTS();
        uploaderObj["lagSeconds"] = datalogLastTS > u.lastSentTS() ? datalogLastTS - u.lastSentTS() : 0;
        uploaderObj["successTotal"] = u.successTotal();
        uploaderObj["failureTotal"] = u.failureTotal();
        uploaderObj["consecutiveFailures"] = u.consecutiveFailures();
        uploaderObj["lastSuccessAt"] = (uint32_t) u.lastSuccessAt();
        uploaderObj["lastAttemptAt"] = (uint32_t) u.lastAttemptAt();
        uploaderObj["lastHttpStatus"] = u.lastHttpStatus();
    });

    String response;
    serializeJson(doc, response);

    server.send(200, contentTypeJSON, response);
}

void handleLogs() {
    WdtHoldGuard hold;

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

    if (!sd.exists(MESSAGE_LOG_PATH)) {
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }

    if (auto f = sd.open(MESSAGE_LOG_PATH, O_READ); f) {
        const size_t fileSize = f.size();
        if (startOffset >= fileSize) {
            server.send(204, contentTypePlain, "");
            f.close();
            return;
        }
        if (startOffset > 0 && !f.seek(startOffset)) {
            returnInternalError("could not seek log");
            f.close();
            return;
        }

        size_t remaining = fileSize - startOffset;
        if (limitBytes > 0 && limitBytes < remaining) {
            remaining = limitBytes;
        }
        if (remaining == 0) {
            server.send(204, contentTypePlain, "");
            f.close();
            return;
        }

        if (!server.chunkedResponseModeStart(200, contentTypePlain)) {
            server.send(505, contentTypePlain, F("HTTP1.1 required"));
            f.close();
            return;
        }

        uint8_t buffer[1024];
        while (remaining > 0) {
            const int readLen = f.read(buffer, min(remaining, sizeof(buffer)));
            if (readLen <= 0) {
                break;
            }
            server.sendContent(reinterpret_cast<char *>(buffer), static_cast<size_t>(readLen));
            wdtFeed();
            remaining -= static_cast<size_t>(readLen);
        }
        server.chunkedResponseFinalize();
        f.close();
        return;
    }

    server.send(404, contentTypeJSON, "Not Found");
}

void handleLogsTrunc() {
    WdtHoldGuard hold;

    LOGI("Log truncation requested");

    constexpr char   restartMarker[] = "**** RESTART ****";
    constexpr size_t restartMarkerLen = sizeof(restartMarker) - 1;
    constexpr char   tempPath[] = MESSAGE_LOG_PATH ".trunc";

    if (!sd.exists(MESSAGE_LOG_PATH)) {
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }

    auto src = sd.open(MESSAGE_LOG_PATH, O_READ);
    if (!src) {
        returnInternalError("could not open log");
        return;
    }

    constexpr size_t chunkSize = 1024;
    uint8_t          window[chunkSize + restartMarkerLen - 1];
    uint8_t          overlap[restartMarkerLen - 1];
    size_t           overlapLen = 0;
    uint32_t         lastMarkerOffset = 0;
    bool             markerFound = false;

    for (uint32_t chunkEnd = src.size(); chunkEnd > 0 && !markerFound;) {
        wdtFeed();

        const uint32_t chunkStart = (chunkEnd > chunkSize) ? (chunkEnd - chunkSize) : 0;
        const size_t   chunkLen = chunkEnd - chunkStart;

        if (!src.seek(chunkStart)) {
            src.close();
            returnInternalError("could not seek log");
            return;
        }

        const int readLen = src.read(window, chunkLen);
        if (readLen < 0 || static_cast<size_t>(readLen) != chunkLen) {
            src.close();
            returnInternalError("could not read log");
            return;
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

    const uint32_t startOffset = markerFound ? lastMarkerOffset : 0;
    if (!src.seek(startOffset)) {
        src.close();
        returnInternalError("could not seek log");
        return;
    }

    auto tmp = sd.open(tempPath, O_WRITE | O_CREAT | O_TRUNC);
    if (!tmp) {
        src.close();
        returnInternalError("could not open temp log");
        return;
    }

    uint8_t buffer[1024];
    while (true) {
        const int readLen = src.read(buffer, sizeof(buffer));
        if (readLen <= 0) {
            break;
        }

        wdtFeed();

        if (tmp.write(buffer, static_cast<size_t>(readLen)) != static_cast<size_t>(readLen)) {
            tmp.close();
            src.close();
            sd.remove(tempPath);
            returnInternalError("could not write temp log");
            return;
        }
    }

    tmp.flush();
    tmp.close();
    src.close();

    sd.remove(MESSAGE_LOG_PATH);
    if (!sd.rename(tempPath, MESSAGE_LOG_PATH)) {
        sd.remove(tempPath);
        returnInternalError("could not replace log");
        return;
    }

    LOGD("Log truncated at offset %u", startOffset);

    server.send(204, contentTypePlain, "");
}

static bool    otaUploadFailed = false;
static uint8_t otaErrorCode = UPDATE_ERROR_OK;
static bool    otaHeld = false;

// UploadHoldRelease drops the watchdog hold taken when an upload started once
// the upload has ended or aborted, whichever way the handler returns.
struct UploadHoldRelease {
    bool &held;
    bool  release;

    ~UploadHoldRelease() {
        if (release && held) {
            wdtRelease();
            held = false;
        }
    }
};

void handleOtaFinish() {
    if (otaUploadFailed || Update.hasError()) {
        String msg = F("{\"error\":\"Update failed\",\"code\":");
        msg.concat(otaErrorCode);
        msg.concat("}");
        server.send(500, contentTypeJSON, msg);

        LOGE("OTA: update failed with code %u", otaErrorCode);

        return;
    }

    LOGI("OTA: update finished, rebooting");

    server.send(204, contentTypePlain, "");
    safeReboot();
}

void handleOtaUpload() {
    HTTPUpload &upload = server.upload();

    UploadHoldRelease holdRelease{otaHeld, upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED};

    if (upload.status == UPLOAD_FILE_START) {
        if (!otaHeld) {
            wdtHold();
            otaHeld = true;
        }
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
        wdtFeed();
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaUploadFailed = true;
            otaErrorCode = Update.getError();
            LOGE("OTA: write failed (%u)", otaErrorCode);
            return;
        }

        LOGD("OTA: written %u bytes", upload.totalSize);
    } else if (upload.status == UPLOAD_FILE_END && !otaUploadFailed) {
        wdtFeed();
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

static bool   publicUploadHeld = false;
static bool   publicUploadFailed = false;
static int    publicUploadStatus = 200;
static String publicUploadError;
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

    UploadHoldRelease holdRelease{publicUploadHeld,
                                  upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED};

    if (upload.status == UPLOAD_FILE_START) {
        if (!publicUploadHeld) {
            wdtHold();
            publicUploadHeld = true;
        }
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

        publicUploadFile = sd.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
        if (!publicUploadFile) {
            publicUploadFailed = true;
            publicUploadStatus = 500;
            publicUploadError = F("Failed to open file");
            LOGE("Public upload: failed to open %s", path.c_str());
        }
    } else if (upload.status == UPLOAD_FILE_WRITE && !publicUploadFailed) {
        wdtFeed();
        if (publicUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
            publicUploadFailed = true;
            publicUploadStatus = 500;
            publicUploadError = F("Write failed");
            Serial.printf("Public upload: write failed at %u bytes", upload.totalSize);
            publicUploadFile.close();
        }
    } else if (upload.status == UPLOAD_FILE_END && !publicUploadFailed) {
        publicUploadFile.flush();
        publicUploadFile.close();

        LOGI("Public upload: complete (%u bytes)", upload.totalSize);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        publicUploadFailed = true;
        publicUploadStatus = 500;
        publicUploadError = F("Upload aborted");

        if (publicUploadFile) {
            publicUploadFile.close();
        }

        LOGE("Public upload: aborted");
    }
}

void handleNotFound() {
    WdtHoldGuard hold;

    LOGD("NotFound requested URL: %s", server.uri().c_str());

    if (server.method() != HTTP_GET) {
        server.send(405, contentTypePlain, "Method Not Allowed");
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
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
        return;
    }

    if (auto f = sd.open(path.c_str(), O_READ); f) {
        if (f.isDirectory()) {
            f.close();
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
            return;
        }

        uint8_t buffer[1024];
        while (true) {
            const int readLen = f.read(buffer, sizeof(buffer));
            if (readLen <= 0) {
                break;
            }
            server.sendContent(reinterpret_cast<char *>(buffer), static_cast<size_t>(readLen));
            wdtFeed();
        }
        server.chunkedResponseFinalize();
        f.close();
        return;
    }

    server.send(404, contentTypeJSON, "Not Found");
}
