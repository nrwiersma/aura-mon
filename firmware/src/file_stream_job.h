//
// Created by Nicholas Wiersma on 2026/09/09.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <HttpRequest.h>

// Streams a file (or a byte range of one) from the SD card into a held
// response, one bounded chunk per c0Queue invocation, so a large transfer
// never blocks other core0 tasks. The job owns itself: it deletes itself
// when the transfer completes or the client goes away.
//
// Usage from a route handler:
//     auto *job = FileStreamJob::open(req, path, {...});
//     if (!job) return;                       // error already sent
//     c0Queue.add(&FileStreamJob::stepTask, 6, job);
class FileStreamJob {
public:
    struct Options {
        uint32_t startOffset = 0;   // skip this many leading bytes
        uint32_t limitBytes = 0;    // 0 = to EOF
        const char *contentType = "text/plain";
        const char *extraHeaders = nullptr;  // e.g. "Content-Encoding: gzip\r\n"
    };

    // Opens the file and parks the request (req.hold) on success. Returns
    // nullptr after sending an error response if the file can't be served.
    static FileStreamJob *open(HttpRequest &req, const std::string &path, const Options &opts);

    // c0Queue trampoline.
    static uint32_t stepTask(void *param);

private:
    FileStreamJob(HttpRequest &req, const Options &opts) : _req(req), _opts(opts) {}

    uint32_t step();
    ~FileStreamJob();

    static constexpr size_t kChunkSize = 2048;

    HttpRequest &_req;
    Options _opts;
    FsFile _src;
    size_t _remaining = 0;
    uint8_t _buf[kChunkSize];
};
