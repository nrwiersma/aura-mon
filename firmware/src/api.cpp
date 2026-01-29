//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";
const char *contentTypeHTML PROGMEM = "text/html";
const char *contentTypeCSV PROGMEM = "text/csv";

void returnOK();
void handleGetConfig();
void handlePostConfig();
void handleEnergy();
void handleLogs();
void handleNotFound();

void setupAPI() {
    server.on("/config", HTTP_GET, handleGetConfig);
    server.on("/config", HTTP_POST, handlePostConfig);
    server.on("/energy", HTTP_GET, handleEnergy);
    server.on("/logs", HTTP_GET, handleLogs);
    // Metrics
    // Stats (momentary)
    server.on("/readyz", HTTP_GET, returnOK);
    server.on("/livez", HTTP_GET, returnOK);
    server.onNotFound(handleNotFound); // Serve "public" from SD Card.
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

void appendCSVValue(String &row, double value, uint8_t precision = 3) {
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

void handleLogs() {
    if (!mutex_enter_block_until(&sdMu, 100)) {
        server.send(408, contentTypePlain, "Request Timeout");
        return;
    }

    if (!sd.exists(MESSAGE_LOG_PATH)) {
        mutex_exit(&sdMu);
        server.send(404, contentTypeJSON, F("{\"error\":\"Not Found\"}"));
    }

    if (auto f = sd.open(MESSAGE_LOG_PATH, O_READ); f) {
        if (!server.chunkedResponseModeStart(200, contentTypePlain)) {
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

    server.send(200, contentTypePlain, "");
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

    String header = F("timestamp");
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
            appendCSVValue(row, energyWh);
            appendCSVValue(row, powerFactor, 4);
        }

        row += "\n";
        server.sendContent(row);
        prevRec = rec;
    }

    LOGD("energy: completed response");

    server.chunkedResponseFinalize();
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

    if (!sd.exists(path.c_str())) {
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
        if (path.endsWith(".html")) {
            contentType = F("text/html");
        } else if (path.endsWith(".css")) {
            contentType = F("text/css");
        } else if (path.endsWith(".js")) {
            contentType = F("application/javascript");
        } else if (path.endsWith(".json")) {
            contentType = contentTypeJSON;
        } else if (path.endsWith(".png")) {
            contentType = F("image/png");
        } else if (path.endsWith(".jpg") || path.endsWith(".jpeg")) {
            contentType = F("image/jpeg");
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
