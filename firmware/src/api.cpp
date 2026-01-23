//
// Created by Nicholas Wiersma on 2025/11/04.
//

#include "auramon.h"

const char *contentTypeJSON PROGMEM = "application/json";
const char *contentTypePlain PROGMEM = "text/plain";
const char *contentTypeHTML PROGMEM = "text/html";
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

    // use HTTP/1.1 Chunked response to avoid building a huge temporary string
    if (!server.chunkedResponseModeStart(200, "text/json")) {
        server.send(505, contentTypeHTML, F("HTTP1.1 required"));
        return;
    }

    // TODO: Stream data from datalog.

    server.chunkedResponseFinalize();
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
