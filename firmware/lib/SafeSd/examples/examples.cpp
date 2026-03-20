//
// SafeSd usage examples.
//
// These examples reflect the actual call-site patterns in this firmware.
// They are not compiled as part of the build; they exist for reference.
//
// Three rules of thumb:
//
//   1. Call SafeSdFile and SafeSdFs methods directly for independent
//      operations — per-call locking is applied automatically.
//
//   2. Wrap in with() when two or more operations must be atomic — a
//      seek+read pair, an exists+open check, or a remove+rename sequence.
//
//   3. Use tryWith() instead of with() wherever the caller must not block
//      indefinitely (HTTP handlers, anything with a response deadline).
//

#include <SafeSd.h>

// Global — replaces: SdFs sd; mutex_t sdMu;
SafeSdFs sd;

// ----------------------------------------------------------------
// Example 1 — Per-call locking, no with() needed.
//
// Use case: Logger. Opens and writes a message on every call.
// Each method locks briefly, then releases.
// ----------------------------------------------------------------
void logMessage(const char *path, const char *msg, size_t len) {
    SafeSdFile f = sd.open(path, O_WRONLY | O_CREAT | O_APPEND);
    if (!f) return;

    f.write(msg, len);
    f.close();
}


// ----------------------------------------------------------------
// Example 2 — Atomic multi-step operation (config save pattern).
//
// Use case: saveConfig(). The remove+rename must not be interleaved
// with another core reading the config file. with() holds the mutex
// across all steps, including the f.write() calls inside serializeJson.
// ----------------------------------------------------------------
std::expected<void, String> saveConfig(const char *tmpPath,
                                       const char *finalPath,
                                       const char *data, size_t len) {
    return sd.with([&](auto &fs) -> std::expected<void, String> {
        SafeSdFile f = fs.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
        if (!f)
            return std::unexpected<String>("could not open tmp file");

        if (f.write(data, len) != len) {
            f.close();
            return std::unexpected<String>("write failed");
        }
        f.flush();
        f.close();

        fs.remove(finalPath);
        if (!fs.rename(tmpPath, finalPath))
            return std::unexpected<String>("rename failed");

        return {};
    });
}


// ----------------------------------------------------------------
// Example 3 — Atomic open, then granular per-chunk reads.
//
// Use case: HTTP static file streaming (handleNotFound).
// The exists+open check must be atomic. After that, each f.read()
// locks only for the duration of the SD read — the lock is NOT
// held during network transmission.
// ----------------------------------------------------------------
void streamFile(SafeSdFs &sd, const char *path) {
    SafeSdFile f;

    auto opened = sd.tryWith(100, [&](auto &fs) -> bool {
        if (!fs.exists(path)) return false;
        f = fs.open(path, O_RDONLY);
        if (f.isDirectory()) { f.close(); return false; }
        return (bool)f;
    });
    if (!opened)  return; // timeout   → respond 408
    if (!*opened) return; // not found → respond 404

    uint8_t buffer[1024];
    while (true) {
        int n = f.read(buffer, sizeof(buffer));
        if (n <= 0) break;
        // server.sendContent(reinterpret_cast<char *>(buffer), n);
    }
    f.close();
}


// ----------------------------------------------------------------
// Example 4 — Atomic seek + read (DataLog pattern).
//
// Use case: DataLog::readRev(). The seek and read on the same
// persistent SafeSdFile must not be separated by another core.
// with() holds the mutex across both calls. The SafeSdFs& parameter
// is unused — the atomicity comes from the mutex, not the parameter.
// ----------------------------------------------------------------
class PersistentLog {
public:
    bool begin(SafeSdFs &sd, const char *path) {
        _sd   = &sd;
        _file = sd.open(path, O_RDWR | O_CREAT);
        return (bool)_file;
    }

    bool readAt(uint32_t pos, void *buf, size_t sz) {
        return _sd->with([&](auto &) -> bool {
            if (!_file.seek(pos)) return false;
            return _file.read(buf, sz) == sz;
        });
    }

    bool writeAt(uint32_t pos, const void *buf, size_t sz) {
        return _sd->with([&](auto &) -> bool {
            if (!_file.seek(pos))           return false;
            if (_file.write(buf, sz) != sz) return false;
            return _file.flush();
        });
    }

private:
    SafeSdFs  *_sd   = nullptr;
    SafeSdFile _file;
};


// ----------------------------------------------------------------
// Example 5 — tryWith() with a typed return value.
//
// Use case: HTTP log handler (handleLogs). The exists+open must be
// atomic, and must time out rather than block indefinitely.
// After that, each read locks only for the SD access.
// ----------------------------------------------------------------
void handleLogs(SafeSdFs &sd) {
    uint32_t   fileSize = 0;
    SafeSdFile logFile;

    auto opened = sd.tryWith(100, [&](auto &fs) -> bool {
        if (!fs.exists("aura-mon/log.txt")) return false;
        logFile  = fs.open("aura-mon/log.txt", O_RDONLY);
        fileSize = logFile.size();
        return (bool)logFile;
    });
    if (!opened)  return; // lock timeout   → respond 408
    if (!*opened) return; // file not found → respond 404

    uint8_t buf[1024];
    size_t  remaining = fileSize;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
        int n = logFile.read(buf, chunk);
        if (n <= 0) break;
        // server.sendContent(buf, n);
        remaining -= static_cast<size_t>(n);
    }
    logFile.close();
}


// ----------------------------------------------------------------
// Example 6 — Cross-callback file upload (handlePublicUpload).
//
// The open uses tryWith() to avoid blocking indefinitely. Subsequent
// chunk writes and the final close lock per-call — no persistent
// outer lock and no state flag are needed between callbacks.
// ----------------------------------------------------------------
static SafeSdFile uploadFile;
static bool       uploadFailed = false;

void onUploadStart(SafeSdFs &sd, const char *path) {
    uploadFailed = false;
    auto ok = sd.tryWith(100, [&](auto &fs) -> bool {
        uploadFile = fs.open(path, O_WRONLY | O_CREAT | O_TRUNC);
        return (bool)uploadFile;
    });
    if (!ok || !*ok) uploadFailed = true;
}

void onUploadChunk(const uint8_t *buf, size_t len) {
    if (uploadFailed) return;
    if (uploadFile.write(buf, len) != len) uploadFailed = true;
}

void onUploadEnd() {
    uploadFile.flush();
    uploadFile.close();
}


// ----------------------------------------------------------------
// Example 7 — Card status check (syncState pattern).
//
// with() holds the mutex across the status and errorCode reads,
// ensuring no other SD operation runs between them.
// ----------------------------------------------------------------
void checkCard(SafeSdFs &sd) {
    bool    present = false;
    uint8_t errCode = 0;

    sd.with([&](auto &fs) {
        auto *c = fs.card();
        present = c->status() != 0 && c->errorCode() == 0;
        errCode = c->errorCode();
    });

    if (!present) {
        // Handle SD error — e.g. ledState = LEDColor::Red;
        (void)errCode;
    }
}


// ----------------------------------------------------------------
// Example 8 — Pre-reboot lock.
// ----------------------------------------------------------------
void safeReboot(SafeSdFs &sd) {
    sd.lockForever();
    delay(100);
    rp2040.reboot();
}

