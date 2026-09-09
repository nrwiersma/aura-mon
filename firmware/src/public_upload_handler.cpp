//
// Created by Nicholas Wiersma on 2026/01/23.
//

#include "auramon.h"

#include "public_upload_handler.h"
#include <HttpRequest.h>

void PublicUploadHandler::fail(int statusCode, const char *reason) {
    _failed = true;
    _statusCode = statusCode;
    _errorReason = reason;
}

void PublicUploadHandler::releaseIfHeld() {
    if (_mutexHeld) {
        mutex_exit(&sdMu);
        _mutexHeld = false;
    }
}

void PublicUploadHandler::onUploadStart(const std::string &name, const std::string &filename) {
    if (name != "file") {
        LOGE("Public upload: unexpected form field name: %s", name.c_str());
        fail(400, "Unexpected form field name");
        return;
    }

    if (filename.empty() || filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        LOGE("Public upload: invalid filename: %s", filename.c_str());
        fail(400, "Invalid filename");
        return;
    }

    std::string path = "public/" + filename;

    LOGI("Public upload: start %s", path.c_str());

    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        LOGE("Public upload: failed to acquire sdMu");
        fail(408, "Request Timeout");
        return;
    }
    _mutexHeld = true;

    _file = sd.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
    if (!_file) {
        LOGE("Public upload: failed to open %s", path.c_str());
        releaseIfHeld();
        fail(500, "Failed to open file");
    }
}

void PublicUploadHandler::onUploadWrite(const uint8_t *data, size_t len) {
    if (_failed) {
        return;
    }

    if (_file.write(data, len) != len) {
        LOGE("Public upload: write failed");
        _file.close();
        releaseIfHeld();
        fail(500, "Write failed");
    }
}

void PublicUploadHandler::onUploadEnd() {
    if (_failed) {
        return;
    }

    _file.flush();
    _file.close();
    releaseIfHeld();

    LOGI("Public upload: complete");
}

void PublicUploadHandler::onUploadAborted() {
    if (_file) {
        _file.close();
    }
    releaseIfHeld();
    if (!_failed) {
        fail(500, "Upload aborted");
    }
    LOGE("Public upload: aborted");
}

void PublicUploadHandler::finish(HttpRequest &req) {
    if (_failed) {
        std::string msg = "{\"error\":\"Upload failed\",\"reason\":\"" + _errorReason + "\"}";
        req.send(_statusCode, "application/json", msg);
        return;
    }
    req.send(204, "text/plain", "");
}
