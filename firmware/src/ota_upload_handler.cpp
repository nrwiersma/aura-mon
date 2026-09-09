//
// Created by Nicholas Wiersma on 2026/01/23.
//

#include "auramon.h"

#include <Updater.h>
#include <LittleFS.h>

#include "ota_upload_handler.h"
#include <ImmediateResponse.h>

void OtaUploadHandler::onUploadStart(const std::string &name, const std::string &filename) {
    (void)filename;

    _failed = false;
    _errorCode = UPDATE_ERROR_OK;
    _bytesWritten = 0;
    Update.clearError();

    if (name != "firmware") {
        _failed = true;
        _errorCode = UPDATE_ERROR_NO_DATA;
        LOGE("OTA: unexpected form field name: %s", name.c_str());
        return;
    }

    FSInfo i;
    LittleFS.begin();
    LittleFS.info(i);
    const uint32_t updateSize = i.totalBytes - i.usedBytes;

    LOGI("OTA: start upload size=%u", updateSize);

    if (!Update.begin(updateSize)) {
        _failed = true;
        _errorCode = Update.getError();
        LOGE("OTA: begin failed (%u)", _errorCode);
        return;
    }

    _updateStarted = true;
    LOGD("OTA: update started");
}

void OtaUploadHandler::onUploadWrite(const uint8_t *data, size_t len) {
    if (_failed) {
        return;
    }

    if (Update.write(const_cast<uint8_t *>(data), len) != len) {
        _failed = true;
        _errorCode = Update.getError();
        LOGE("OTA: write failed (%u)", _errorCode);
        return;
    }
    _bytesWritten += len;
}

void OtaUploadHandler::onUploadEnd() {
    if (_failed) {
        return;
    }

    if (_bytesWritten == 0) {
        // Never commit an empty image: Update.end(true) would otherwise
        // succeed with zero bytes and we'd reboot into a blank firmware.
        _failed = true;
        _errorCode = UPDATE_ERROR_NO_DATA;
        if (_updateStarted) {
            Update.end();  // discard
        }
        LOGE("OTA: no firmware data received");
        return;
    }

    if (!Update.end(true)) {
        _failed = true;
        _errorCode = Update.getError();
        LOGE("OTA: end failed (%u)", _errorCode);
        return;
    }

    LOGI("OTA: upload complete");
}

void OtaUploadHandler::onUploadAborted() {
    _failed = true;
    _errorCode = UPDATE_ERROR_STREAM;
    if (_updateStarted) {
        Update.end();
    }
    LOGE("OTA: upload aborted");
}

std::unique_ptr<HttpResponseProducer> OtaUploadHandler::finish() {
    if (_failed || Update.hasError()) {
        String msg = F("{\"error\":\"Update failed\",\"code\":");
        msg.concat(_errorCode);
        msg.concat("}");
        LOGE("OTA: update failed with code %u", _errorCode);
        return std::make_unique<ImmediateResponse>(500, "application/json", msg.c_str());
    }

    LOGI("OTA: update finished, rebooting");
    // Defer the reboot to the next loop() iteration so this response has
    // already been handed off to the transport (tcp_output()) before we
    // reset - rebooting from inside finish() itself would happen before
    // pump() ever gets a chance to write it out.
    c0Queue.add([](void *) -> uint32_t {
        safeReboot();
        return 0;
    }, 10);

    return std::make_unique<ImmediateResponse>(204, "text/plain", "");
}
