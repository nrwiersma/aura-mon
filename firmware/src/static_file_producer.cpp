//
// Created by Nicholas Wiersma on 2026/01/23.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "static_file_producer.h"
#endif

#include "static_file_producer.h"

#include <cstring>

namespace {

bool endsWith(const std::string &s, const char *suffix) {
    size_t len = strlen(suffix);
    return s.size() >= len && s.compare(s.size() - len, len, suffix) == 0;
}

}  // namespace

StaticFileProducer::StaticFileProducer(HttpMethod method, std::string uri) : _method(method), _uri(std::move(uri)) {}

std::string StaticFileProducer::contentTypeForPath(const std::string &path) {
    if (endsWith(path, ".html") || endsWith(path, ".html.gz")) return "text/html";
    if (endsWith(path, ".css") || endsWith(path, ".css.gz")) return "text/css";
    if (endsWith(path, ".js") || endsWith(path, ".js.gz")) return "application/javascript";
    if (endsWith(path, ".json") || endsWith(path, ".json.gz")) return "application/json";
    if (endsWith(path, ".png") || endsWith(path, ".png.gz")) return "image/png";
    if (endsWith(path, ".jpg") || endsWith(path, ".jpeg") || endsWith(path, ".jpg.gz") ||
        endsWith(path, ".jpeg.gz"))
        return "image/jpeg";
    if (endsWith(path, ".ico") || endsWith(path, ".ico.gz")) return "image/x-icon";
    if (endsWith(path, ".svg") || endsWith(path, ".svg.gz")) return "image/svg+xml";
    return "text/plain";
}

HttpResponseProducer::Status StaticFileProducer::fail(int statusCode, const char *reason) {
    _statusCode = statusCode;
    _errorReason = reason;
    _state = State::Error;
    return Status::Error;
}

HttpResponseProducer::Status StaticFileProducer::produce(uint8_t *buf, size_t cap, size_t &written) {
    written = 0;

    switch (_state) {
        case State::Start: {
            if (_method != HttpMethod::GET) {
                return fail(405, "Method Not Allowed");
            }

            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }

            std::string path = _uri;
            if (path.empty() || path[0] != '/') {
                path = "/" + path;
            }
            if (path == "/") {
                path = "/index.html";
            }
            path = "public" + path;
            std::string gzPath = path + ".gz";

            if (sd.exists(gzPath.c_str())) {
                _gzip = true;
                path = gzPath;
            } else if (!sd.exists(path.c_str())) {
                mutex_exit(&sdMu);
                return fail(404, "Not Found");
            }

            _src = sd.open(path.c_str(), O_READ);
            if (!_src) {
                mutex_exit(&sdMu);
                return fail(500, "could not open file");
            }
            if (_src.isDirectory()) {
                _src.close();
                mutex_exit(&sdMu);
                return fail(403, "Forbidden");
            }
            mutex_exit(&sdMu);

            _contentType = contentTypeForPath(path);
            _state = State::Copy;
            return Status::Pending;
        }

        case State::Copy: {
            if (!mutex_enter_timeout_ms(&sdMu, 100)) {
                return Status::Pending;
            }
            const int readLen = _src.read(buf, min(cap, kChunkSize));
            mutex_exit(&sdMu);

            if (readLen <= 0) {
                _src.close();
                _state = State::Done;
                return Status::Done;
            }

            written = static_cast<size_t>(readLen);
            return Status::Data;
        }

        case State::Done:
        case State::Error:
        default:
            return Status::Done;
    }
}
