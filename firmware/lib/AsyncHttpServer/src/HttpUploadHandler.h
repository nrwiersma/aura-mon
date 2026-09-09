//
// HttpUploadHandler - handles one multipart/form-data upload end-to-end,
// driven incrementally by HttpConnection/MultipartParser as raw body bytes
// stream in off the wire, so a multi-megabyte upload (e.g. an OTA firmware
// image) never needs to be buffered in memory.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "HttpResponseProducer.h"

class HttpUploadHandler {
public:
    virtual ~HttpUploadHandler() = default;

    // Called once a new file part begins (its Content-Disposition name and
    // filename attributes; filename is empty for a non-file form field,
    // which callers are free to ignore).
    virtual void onUploadStart(const std::string &name, const std::string &filename) = 0;

    // Called with each chunk of that file's raw bytes, strictly in order.
    virtual void onUploadWrite(const uint8_t *data, size_t len) = 0;

    // Called once the file part's data has been fully delivered.
    virtual void onUploadEnd() = 0;

    // Called instead of onUploadEnd() if the connection is closed/reset
    // before the upload completed, or the multipart body was malformed.
    virtual void onUploadAborted() = 0;

    // Called exactly once, after onUploadEnd() or onUploadAborted(), to
    // build the final HTTP response describing the outcome.
    virtual std::unique_ptr<HttpResponseProducer> finish() = 0;
};
