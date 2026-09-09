//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <HttpUploadHandler.h>

// Streams a firmware image straight into the flash Updater as multipart
// bytes arrive off the wire, so an OTA upload of several megabytes never
// needs to be buffered in RAM or on the SD card.
class OtaUploadHandler : public HttpUploadHandler {
public:
    void onUploadStart(const std::string &name, const std::string &filename) override;
    void onUploadWrite(const uint8_t *data, size_t len) override;
    void onUploadEnd() override;
    void onUploadAborted() override;
    void finish(HttpRequest &req) override;

private:
    bool     _failed = false;
    uint8_t  _errorCode = 0;
    bool     _updateStarted = false;
    uint32_t _bytesWritten = 0;  // total firmware bytes received this upload
};
