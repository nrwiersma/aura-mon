//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";
const char *contentTypeCSV PROGMEM = "text/csv";


void returnOK();
void handleEnergy();
void handleNotFound();

void setupAPI() {
    server.on("/energy", HTTP_GET, handleEnergy);
    // Config
    // Log
    // Metrics
    // Stats (momentary)
    server.on("/readyz", HTTP_GET, returnOK);
    server.on("/livez", HTTP_GET, returnOK);
    server.onNotFound(handleNotFound); // Serve "public" from SD Card.
}

void returnOK() {
    server.send(200, contentTypePlain, "");
}

void returnInternalError(const char* reason) {
    String msg = "{\"error\":\"Internal Error\",\"reason\":\"";
    msg.concat(reason);
    msg.concat("\"}");
    server.send(500, contentTypeJSON, msg);
}

void handleEnergy() {
    uint32_t baseInterval = datalog.interval();
    uint32_t start = server.arg("start").toInt();
    uint32_t end = server.hasArg("end") ? server.arg("end").toInt(): time(nullptr);
    uint32_t interval = server.hasArg("interval") ? server.arg("interval").toInt() : 5;


    start -= start % baseInterval;
    end -= end % baseInterval;
    interval -= interval % baseInterval;

    if (start >= end || interval == 0) {
        server.send(400, contentTypeJSON, F("{\"error\":\"Invalid parameters\"}"));
        return;
    }

    // auto resp = req->beginResponseStream(contentTypeCSV);
    // resp->setCode(200);
    //
    // logRecord rec;
    // if (auto ret = datalog.read(start - interval, &rec); ret != 0) {
    //     returnInternalError(readError(ret));
    //     return;
    // }
    // auto prevRec = rec;
    //
    // // TODO: check we got the timestamp we want.
    //
    // JsonDocument doc;
    // JsonArray arr = doc.to<JsonArray>();
    // for (uint32_t ts = start; ts <= end; ts += interval) {
    //     if (auto ret = datalog.read(ts, &rec); ret != 0) {
    //         returnInternalError(readError(ret));
    //         return;
    //     }
    //
    //     double elapsedHours = rec.logHours - prevRec.logHours;
    //
    //     JsonObject obj = arr.add<JsonObject>();
    //     obj["timestamp"] = ts;
    //     for (int i = 0; i < MAX_DEVICES; i++) {
    //
    //         obj["timestamp"] = ts;
    //         obj["VAh"] = rec.vaHrs[i] - prevRec.vaHrs[i];
    //         obj["Wh"] = rec.wattHrs [i]- prevRec.wattHrs[i];
    //     }
    //
    //     prevRec = rec;
    // }
    //
    //
    // server.send(resp);
}

void handleNotFound() {
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

        server.send(f, path, contentType);
        f.close();

        mutex_exit(&sdMu);
        return;
    }
    mutex_exit(&sdMu);

    server.send(404, contentTypeJSON, "Not Found");
}
