//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <HttpUploadHandler.h>

// Streams an uploaded file straight onto the SD card's public/ directory as
// multipart bytes arrive off the wire, one write per chunk, so an upload of
// any size never needs to be buffered in RAM.
class PublicUploadHandler : public HttpUploadHandler {
public:
    void onUploadStart(const std::string &name, const std::string &filename) override;
    void onUploadWrite(const uint8_t *data, size_t len) override;
    void onUploadEnd() override;
    void onUploadAborted() override;
    std::unique_ptr<HttpResponseProducer> finish() override;

private:
    void fail(int statusCode, const char *reason);
    void releaseIfHeld();

    bool _failed = false;
    int _statusCode = 200;
    std::string _errorReason;
    bool _mutexHeld = false;
    FsFile _file;
};
