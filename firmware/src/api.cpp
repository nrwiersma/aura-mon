//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";


void returnOK(AsyncWebServerRequest * req);
void handleEnergy(AsyncWebServerRequest *req);
void handleNotFound(AsyncWebServerRequest * req);

void setupAPI(AsyncWebServer *server) {
    server->on("/energy", HTTP_GET, handleEnergy);
    // Config
    // Log
    // Metrics
    // Stats (momentary)
    server->on("/readyz", HTTP_GET, returnOK);
    server->on("/livez", HTTP_GET, returnOK);
    server->onNotFound(handleNotFound); // Serve "public" from SD Card.
}

void returnOK(AsyncWebServerRequest *req) {
    req->send(200, contentTypePlain, "");
}

void returnInternalError(AsyncWebServerRequest *req, const char* reason) {
    String msg = "{\"error\":\"Internal Error\",\"reason\":\"";
    msg.concat(reason);
    msg.concat("\"}");
    req->send(500, contentTypeJSON, msg);
}

void handleEnergy(AsyncWebServerRequest *req) {
    uint32_t baseInterval = datalog.interval();
    uint32_t start = req->getParam("start")->value().toInt();
    uint32_t end = req->getParam("end")->value().toInt();
    uint32_t interval = req->hasParam("interval") ? req->getParam("interval")->value().toInt() : 5;

    start -= start % baseInterval;
    end -= end % baseInterval;
    interval -= interval % baseInterval;

    if (start >= end || interval == 0) {
        req->send(400, contentTypeJSON, "{\"error\":\"Invalid parameters\"}");
        return;
    }

    logRecord rec;
    if (auto ret = datalog.read(start - interval, &rec); ret != 0) {
        returnInternalError(req, readError(ret));
        return;
    }
    auto prevRec = rec;

    // TODO: check we got the timestamp we want.

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint32_t ts = start; ts <= end; ts += interval) {
        if (auto ret = datalog.read(ts, &rec); ret != 0) {
            returnInternalError(req, readError(ret));
            return;
        }

        double elapsedHours = rec.logHours - prevRec.logHours;

        JsonObject obj = arr.add<JsonObject>();
        obj["timestamp"] = ts;
        for (int i = 0; i < MAX_DEVICES; i++) {

            obj["timestamp"] = ts;
            obj["VAh"] = rec.vaHrs[i] - prevRec.vaHrs[i];
            obj["Wh"] = rec.wattHrs [i]- prevRec.wattHrs[i];
        }

        prevRec = rec;
    }

    String json;
    serializeJson(arr, json);
    req->send(200, contentTypeJSON, json);
}

void handleNotFound(AsyncWebServerRequest *req) {
    if (req->method() != HTTP_GET) {
        req->send(405, contentTypePlain, "Method Not Allowed");
        return;
    }

    if (!mutex_enter_block_until(&sdMu, 100)) {
        req->send(408, contentTypePlain, "Request Timeout");
        return;
    }

    String path = req->url();
    if (!path.startsWith("/")) path = '/' + path;
    if (path == "/") path = "/index.html";
    path = "public" + path;

    if (!sd.exists(path.c_str())) {
        mutex_exit(&sdMu);
        req->send(404, contentTypeJSON, "{\"error\":\"Not Found\"}");
    }

    if (auto f = sd.open(path.c_str(), O_READ); f) {
        if (f.isDirectory()) {
            f.close();
            mutex_exit(&sdMu);
            req->send(403, contentTypePlain, "Forbidden");
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

        req->send(f, path, contentType);
        f.close();

        mutex_exit(&sdMu);
        return;
    }
    mutex_exit(&sdMu);

    req->send(404, contentTypeJSON, "Not Found");
}
