//
// Created by Nicholas Wiersma on 2026/09/09.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif

#include "file_stream_job.h"

FileStreamJob *FileStreamJob::open(HttpRequest &req, const std::string &path, const Options &opts) {
    auto fail = [&req](int statusCode, const char *reason) -> FileStreamJob * {
        req.send(statusCode, "text/plain", reason);
        return nullptr;
    };

    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        return fail(408, "Request Timeout");
    }
    if (!sd.exists(path.c_str())) {
        mutex_exit(&sdMu);
        return fail(404, "Not Found");
    }
    FsFile src = sd.open(path.c_str(), O_READ);
    mutex_exit(&sdMu);
    if (!src) {
        return fail(500, "could not open file");
    }
    if (src.isDirectory()) {
        src.close();
        return fail(403, "Forbidden");
    }

    const size_t fileSize = src.size();
    if (opts.startOffset >= fileSize) {
        src.close();
        req.send(204, "text/plain", "");
        return nullptr;
    }
    if (opts.startOffset > 0 && !src.seek(opts.startOffset)) {
        src.close();
        return fail(500, "could not seek file");
    }

    auto *job = new FileStreamJob(req, opts);
    job->_src = src;
    job->_remaining = fileSize - opts.startOffset;
    if (opts.limitBytes > 0 && opts.limitBytes < job->_remaining) {
        job->_remaining = opts.limitBytes;
    }

    req.hold(200, opts.contentType, opts.extraHeaders);
    return job;
}

uint32_t FileStreamJob::stepTask(void *param) {
    return static_cast<FileStreamJob *>(param)->step();
}

uint32_t FileStreamJob::step() {
    if (!_req.alive()) {
        delete this;
        return 0;
    }
    if (_remaining == 0) {
        _req.done();
        delete this;
        return 0;
    }

    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        return 2;  // SD busy; retry shortly
    }
    const size_t toRead = min(_remaining, kChunkSize);
    const int readLen = _src.read(_buf, toRead);
    mutex_exit(&sdMu);

    if (readLen <= 0) {
        // Unexpected EOF/error: end the response cleanly with what we have.
        _req.done();
        delete this;
        return 0;
    }

    if (_req.write(_buf, static_cast<size_t>(readLen)) == 0) {
        // Send window full: rewind so the chunk is re-read whole next time.
        _src.seek(_src.position() - readLen);
        return 10;
    }

    _remaining -= static_cast<size_t>(readLen);
    return 2;
}

FileStreamJob::~FileStreamJob() {
    if (_src) {
        _src.close();
    }
}
