//
// Created by Nicholas Wiersma on 2026/08/20.
//

#include "auramon.h"
#include "uploader/uploader.h"

namespace {
    // How often to poll an in-flight request for completion.
    constexpr uint32_t POLL_DELAY_MS = 50;

    // How long to wait before checking again when there is nothing new to
    // send yet.
    constexpr uint32_t IDLE_DELAY_MS = 1000;

    // How long to back off after the network is down or a request failed,
    // so a disconnected uploader does not spin the task queue.
    constexpr uint32_t RETRY_DELAY_MS = 10000;

    // Run again immediately (but still yield to the scheduler) to drain a
    // backlog quickly once the network/uploader catches up.
    constexpr uint32_t CONTINUE_DELAY_MS = 1;

    constexpr uint32_t REQUEST_TIMEOUT_S = 5;

    // How long the link must have been up before an uploader sends anything,
    // so the stack can settle after (re)connecting.
    constexpr uint32_t NETWORK_SETTLE_MS = 1000;

    bool networkReady() {
        static uint32_t upSince = 0;

        if (!eth.isLinked() || !eth.connected()) {
            upSince = 0;
            return false;
        }
        if (upSince == 0) {
            upSince = millis() | 1;
        }
        return millis() - upSince >= NETWORK_SETTLE_MS;
    }

    constexpr const char *UPLOADER_STATE_DIR = "aura-mon/uploaders";
}

Uploader::Uploader(String id, String type)
    : _id(std::move(id)),
      _type(std::move(type)),
      _interval(60),
      _state(State::Build),
      _pendingToTS(0),
      _lastSentTS(0),
      _progressLoaded(false),
      _stopRequested(false),
      _successTotal(0),
      _failureTotal(0),
      _consecutiveFailures(0),
      _lastSuccessAt(0),
      _lastAttemptAt(0),
      _lastHttpStatus(0),
      _request(nullptr) {
}

Uploader::~Uploader() {
    delete _request;
}

bool Uploader::configure(const UploaderConfig &cfg) {
    if (cfg.id != _id || cfg.type != _type) {
        // Should never happen; the registry replaces the instance instead
        // of reconfiguring it when either the id or the type changes.
        return false;
    }

    _interval = cfg.interval > 0 ? cfg.interval : 60;

    JsonDocument doc;
    if (cfg.settings.length() > 0) {
        if (deserializeJson(doc, cfg.settings.c_str())) {
            LOGE("uploader %s: could not parse settings", _id.c_str());
            return false;
        }
    }
    if (!applySettings(doc.as<JsonObjectConst>())) {
        LOGE("uploader %s: invalid settings", _id.c_str());
        return false;
    }

    return true;
}

void Uploader::statePath(char *buf, size_t len) const {
    snprintf(buf, len, "%s/%s.state", UPLOADER_STATE_DIR, _id.c_str());
}

void Uploader::loadProgress() {
    _progressLoaded = true;

    // Default to "now": a newly configured uploader starts from the current
    // record rather than replaying the entire (up to 180 day) backlog.
    _lastSentTS = datalog.lastTS();

    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        LOGE("uploader %s: could not acquire sdMu to load progress", _id.c_str());
        return;
    }

    char path[64];
    statePath(path, sizeof(path));
    FsFile file = sd.open(path, O_RDONLY);
    if (file) {
        char buf[16] = {};
        auto n = file.read(buf, sizeof(buf) - 1);
        file.close();
        if (n > 0) {
            uint32_t ts = strtoul(buf, nullptr, 10);
            // Only resume from a saved position within the retained log; a
            // stale/garbage value should not wind the uploader back further
            // than the data we actually still have.
            if (ts >= datalog.firstTS() && ts <= datalog.lastTS()) {
                _lastSentTS = ts;
            }
        }
    }

    mutex_exit(&sdMu);
}

void Uploader::saveProgress() {
    if (!mutex_enter_timeout_ms(&sdMu, 100)) {
        LOGE("uploader %s: could not acquire sdMu to save progress", _id.c_str());
        return;
    }

    sd.mkdir(UPLOADER_STATE_DIR);
    char path[64];
    statePath(path, sizeof(path));
    FsFile file = sd.open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (file) {
        char buf[16];
        auto n = snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(_lastSentTS));
        file.write(buf, n);
        file.close();
    } else {
        LOGE("uploader %s: could not save progress", _id.c_str());
    }

    mutex_exit(&sdMu);
}

uint32_t Uploader::dispatch() {
    if (_stopRequested) {
        // Deregister permanently and free ourselves; nothing else holds a
        // reference to this instance once this call returns.
        if (_request) {
            _request->abort();
        }
        delete this;
        return 0;
    }

    switch (_state) {
        case State::Build:
            return handleBuild();
        case State::Post:
            return handlePost();
        case State::Wait:
            return handleWait();
    }
    return IDLE_DELAY_MS;
}

uint32_t Uploader::handleBuild() {
    if (!networkReady()) {
        // Network is down: do nothing. The data log keeps every record on
        // SD, so there is nothing to lose by waiting - we simply catch up
        // once the link returns.
        return RETRY_DELAY_MS;
    }

    if (!_progressLoaded) {
        loadProgress();
    }

    const uint32_t toTS = _lastSentTS + _interval;
    if (datalog.lastTS() < toTS) {
        // Not enough new data yet for a full interval.
        return IDLE_DELAY_MS;
    }

    _body.flush();
    _contentType = "";
    if (!buildRequest(_lastSentTS, toTS, _body, _contentType)) {
        // Nothing to send for this interval (e.g. no enabled sensors). Skip
        // past it rather than spinning on it forever.
        _lastSentTS = toTS;
        saveProgress();
        return CONTINUE_DELAY_MS;
    }

    _pendingToTS = toTS;
    _state = State::Post;
    return CONTINUE_DELAY_MS;
}

uint32_t Uploader::handlePost() {
    if (!networkReady()) {
        _state = State::Build;
        return RETRY_DELAY_MS;
    }

    if (!_request) {
        _request = new asyncHTTPrequest;
    }
    _request->setTimeout(REQUEST_TIMEOUT_S);

    // lwIP callbacks (connect/error/ack) are processed whenever the lwIP lock is fully released,
    // so hold it across open() and send(); otherwise the connection can be torn down in between.
    LwipLock lwipLock;

    if (!_request->open(httpMethod(), endpoint().c_str())) {
        LOGE("uploader %s: could not open request to %s", _id.c_str(), endpoint().c_str());
        recordResult(false, -1);
        _body.flush();
        _state = State::Build;
        return RETRY_DELAY_MS;
    }
    setRequestHeaders(*_request);

    bool sent;
    if (strcmp(httpMethod(), "GET") == 0) {
        sent = _request->send();
    } else {
        if (_contentType.length() > 0) {
            _request->setReqHeader("content-type", _contentType.c_str());
        }
        sent = _request->send(&_body, _body.available());
    }
    _body.flush();

    if (!sent) {
        LOGE("uploader %s: send failed", _id.c_str());
        recordResult(false, -1);
        _state = State::Build;
        return RETRY_DELAY_MS;
    }

    _state = State::Wait;
    return POLL_DELAY_MS;
}

uint32_t Uploader::handleWait() {
    if (_request->readyState() != 4) {
        return POLL_DELAY_MS;
    }

    const int code = _request->responseHTTPcode();
    if (code >= 200 && code < 300) {
        recordResult(true, code);
        _lastSentTS = _pendingToTS;
        saveProgress();
        _state = State::Build;
        return CONTINUE_DELAY_MS;
    }

    LOGE("uploader %s: post failed, HTTP %d", _id.c_str(), code);
    recordResult(false, code);
    _state = State::Build;
    return RETRY_DELAY_MS;
}

void Uploader::recordResult(bool success, int httpStatus) {
    _lastAttemptAt = time(nullptr);
    _lastHttpStatus = httpStatus;
    if (success) {
        _successTotal++;
        _consecutiveFailures = 0;
        _lastSuccessAt = _lastAttemptAt;
    } else {
        _failureTotal++;
        _consecutiveFailures++;
    }
}

const char *Uploader::stateName() const {
    switch (_state) {
        case State::Build:
            return "build";
        case State::Post:
            return "post";
        case State::Wait:
            return "wait";
    }
    return "unknown";
}
