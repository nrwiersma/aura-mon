//
// Created by Nicholas Wiersma on 2026/11/17.
//

#include "metrics_query.h"

#include "uploader/uploader.h"
#include "uploader/uploader_registry.h"

namespace {
// A scraper that does not accept data for this long is abandoned.
constexpr uint32_t maxStreamMs = 10000;
// A single write should never wait on the peer for long.
constexpr uint32_t sendTimeoutMs = 500;
// Room needed in the send buffer on top of the payload for the chunk framing.
constexpr int chunkOverhead = 16;
constexpr uint8_t sectionCount = 7;

void head(String &out, const char *name, const char *help, const char *type) {
    out += F("# HELP ");
    out += name;
    out += ' ';
    out += help;
    out += '\n';
    out += F("# TYPE ");
    out += name;
    out += ' ';
    out += type;
    out += '\n';
}

void sample(String &out, const char *name, const String &value) {
    out += name;
    out += ' ';
    out += value;
    out += '\n';
}

void deviceSample(String &out, const char *name, const metricsDevice &dev, const String &value) {
    out += name;
    out += F("{device=\"");
    out += dev.name;
    out += F("\",address=\"");
    out += String(dev.addr);
    out += F("\"} ");
    out += value;
    out += '\n';
}

void uploaderSamples(String &out, const char *name, const std::function<String(const Uploader &)> &value) {
    forEachUploader([&](const Uploader &u) {
        out += name;
        out += F("{id=\"");
        out += u.id();
        out += F("\",type=\"");
        out += u.type();
        out += F("\"} ");
        out += value(u);
        out += '\n';
    });
}
} // namespace

bool metricsQuery::_active = false;

metricsQuery::metricsQuery(HTTPRequest *req) : _req(req),
                                               _startMs(millis()),
                                               _headerSent(false),
                                               _devicesSnapshotted(false),
                                               _section(0),
                                               _deviceCount(0) {
    _active = true;
    _req->setTimeout(sendTimeoutMs);
}

metricsQuery::~metricsQuery() {
    _active = false;
    delete _req;
}

uint32_t metricsQuery::_step() {
    if (millis() - _startMs > maxStreamMs) {
        // Abandon the scrape without the chunk terminator so the truncated
        // body fails the scrape instead of being ingested as a partial exposition.
        _req->abort();
        return 0;
    }
    if (!_req->connected()) {
        _req->finish();
        return 0;
    }

    if (!_headerSent) {
        _req->sendHeader("Cache-Control", "no-cache");
        _req->send(200, "text/plain", String());
        _headerSent = true;
        return 1;
    }

    if (_pending.isEmpty() && !_buildNextSection()) {
        _req->finish();
        return 0;
    }

    // Wait for the peer to drain the send buffer rather than blocking on the write.
    if (_req->availableForWrite() < static_cast<int>(_pending.length()) + chunkOverhead) {
        return 20;
    }

    _req->sendContent(_pending);
    _pending = "";
    return 1;
}

void metricsQuery::_snapshotDevices() {
    _devicesSnapshotted = true;

    if (!mutex_enter_timeout_ms(&deviceInfoMu, 100)) {
        LOGE("metrics: could not acquire deviceInfoMu");
        return;
    }
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        const auto info = deviceInfos[i];
        if (!info || !info->isEnabled()) {
            continue;
        }
        _devices[_deviceCount++] = metricsDevice{i, info->addr, info->name};
    }
    mutex_exit(&deviceInfoMu);
}

bool metricsQuery::_buildNextSection() {
    while (_pending.isEmpty() && _section < sectionCount) {
        switch (_section++) {
            case 0:
                _pending = _modbusSection();
                break;
            case 1:
                if (!_devicesSnapshotted) {
                    _snapshotDevices();
                }
                _pending = _deviceErrorsSection();
                break;
            case 2:
                _pending = _deviceCollectTimeSection();
                break;
            case 3:
                _pending = _datalogSection();
                break;
            case 4:
                _pending = _systemSection();
                break;
            case 5:
                _pending = _uploaderCountersSection();
                break;
            case 6:
                _pending = _uploaderStateSection();
                break;
        }
    }
    return !_pending.isEmpty();
}

String metricsQuery::_modbusSection() {
    String out;
    out.reserve(1024);

    const uint64_t totalMs = metrics.modbus_collect_time_ms_total.load(std::memory_order_relaxed);
    const uint32_t avgMs = metrics.modbus_last_run_avg_ms.load(std::memory_order_relaxed);

    head(out, "auramon_modbus_errors_total", "Total modbus collection errors.", "counter");
    sample(out, "auramon_modbus_errors_total", String(metrics.modbus_errors_total.load(std::memory_order_relaxed)));
    head(out, "auramon_collect_time_seconds_total", "Total time spent collecting data in seconds.", "counter");
    sample(out, "auramon_collect_time_seconds_total", String(totalMs / 1000.0, 6));
    head(out, "auramon_collect_time_seconds_avg",
         "Average per-device collection time for the last run in seconds.", "gauge");
    sample(out, "auramon_collect_time_seconds_avg", String(avgMs / 1000.0, 6));
    head(out, "auramon_modbus_collect_runs_total", "Total number of Modbus collection cycles run.", "counter");
    sample(out, "auramon_modbus_collect_runs_total",
           String(metrics.modbus_collect_runs_total.load(std::memory_order_relaxed)));
    return out;
}

String metricsQuery::_deviceErrorsSection() const {
    String out;
    out.reserve(256 + _deviceCount * 96);

    head(out, "auramon_modbus_device_errors_total", "Total errors per Modbus device.", "counter");
    for (size_t i = 0; i < _deviceCount; i++) {
        const auto &dev = _devices[i];
        const uint32_t errors = metrics.modbus_device_errors_total[dev.index].load(std::memory_order_relaxed);
        deviceSample(out, "auramon_modbus_device_errors_total", dev, String(errors));
    }
    return out;
}

String metricsQuery::_deviceCollectTimeSection() const {
    String out;
    out.reserve(256 + _deviceCount * 96);

    head(out, "auramon_modbus_device_collect_time_seconds",
         "Last successful collection time per Modbus device in seconds.", "gauge");
    for (size_t i = 0; i < _deviceCount; i++) {
        const auto &dev = _devices[i];
        const uint32_t ms = metrics.modbus_device_last_collect_ms[dev.index].load(std::memory_order_relaxed);
        deviceSample(out, "auramon_modbus_device_collect_time_seconds", dev, String(ms / 1000.0, 6));
    }
    return out;
}

String metricsQuery::_datalogSection() {
    String out;
    out.reserve(1536);

    head(out, "auramon_datalog_read_io", "Number of read IO operations performed on the datalog.", "counter");
    sample(out, "auramon_datalog_read_io", String(metrics.datalog_read_io.load(std::memory_order_relaxed)));
    head(out, "auramon_datalog_write_io", "Number of write IO operations performed on the datalog.", "counter");
    sample(out, "auramon_datalog_write_io", String(metrics.datalog_write_io.load(std::memory_order_relaxed)));
    head(out, "auramon_datalog_write_time_seconds_total",
         "Total time spent writing datalog records in seconds.", "counter");
    sample(out, "auramon_datalog_write_time_seconds_total",
           String(metrics.datalog_write_time_ms_total.load(std::memory_order_relaxed) / 1000.0, 6));
    head(out, "auramon_datalog_cache_hit", "Number of cache hits when reading records from the datalog.", "counter");
    sample(out, "auramon_datalog_cache_hit", String(metrics.datalog_cache_hit.load(std::memory_order_relaxed)));
    head(out, "auramon_datalog_write_errors_total", "Total failed datalog write attempts.", "counter");
    sample(out, "auramon_datalog_write_errors_total",
           String(metrics.datalog_write_errors_total.load(std::memory_order_relaxed)));
    head(out, "auramon_datalog_queue_depth", "Number of records waiting to be written to the datalog.", "gauge");
    sample(out, "auramon_datalog_queue_depth", String(metrics.datalog_queue_depth.load(std::memory_order_relaxed)));
    head(out, "auramon_datalog_queue_full_total",
         "Total times a record could not be queued because the queue was full.", "counter");
    sample(out, "auramon_datalog_queue_full_total",
           String(metrics.datalog_queue_full_total.load(std::memory_order_relaxed)));
    head(out, "auramon_datalog_records_dropped_total",
         "Total records dropped after repeated write failures.", "counter");
    sample(out, "auramon_datalog_records_dropped_total",
           String(metrics.datalog_records_dropped_total.load(std::memory_order_relaxed)));
    return out;
}

String metricsQuery::_systemSection() {
    String out;
    out.reserve(1536);

    head(out, "auramon_ntp_syncs_total", "Total successful NTP synchronisations.", "counter");
    sample(out, "auramon_ntp_syncs_total", String(metrics.ntp_syncs_total.load(std::memory_order_relaxed)));
    head(out, "auramon_ntp_failures_total", "Total failed NTP synchronisation attempts.", "counter");
    sample(out, "auramon_ntp_failures_total", String(metrics.ntp_failures_total.load(std::memory_order_relaxed)));
    head(out, "auramon_ntp_offset_ms", "Clock offset applied during the last NTP sync in milliseconds.", "gauge");
    sample(out, "auramon_ntp_offset_ms", String((long) metrics.ntp_last_offset_ms.load(std::memory_order_relaxed)));

    head(out, "auramon_ethernet_disconnects_total", "Total number of Ethernet link-loss events.", "counter");
    sample(out, "auramon_ethernet_disconnects_total",
           String(metrics.ethernet_disconnects_total.load(std::memory_order_relaxed)));

    head(out, "auramon_log_errors_total", "Total number of error-level log messages emitted.", "counter");
    sample(out, "auramon_log_errors_total", String(metrics.log_errors_total.load(std::memory_order_relaxed)));
    return out;
}

String metricsQuery::_uploaderCountersSection() {
    String out;
    out.reserve(1024);

    head(out, "auramon_uploader_requests_total", "Total successful upload requests.", "counter");
    uploaderSamples(out, "auramon_uploader_requests_total", [](const Uploader &u) { return String(u.successTotal()); });
    head(out, "auramon_uploader_errors_total", "Total failed upload attempts.", "counter");
    uploaderSamples(out, "auramon_uploader_errors_total", [](const Uploader &u) { return String(u.failureTotal()); });
    head(out, "auramon_uploader_consecutive_failures",
         "Current number of consecutive failed upload attempts.", "gauge");
    uploaderSamples(out, "auramon_uploader_consecutive_failures",
                    [](const Uploader &u) { return String(u.consecutiveFailures()); });
    return out;
}

String metricsQuery::_uploaderStateSection() {
    String out;
    out.reserve(1024);

    const uint32_t datalogLastTS = datalog.lastTS();
    head(out, "auramon_uploader_lag_seconds",
         "How far behind the uploader is from the most recently logged record, in seconds.", "gauge");
    uploaderSamples(out, "auramon_uploader_lag_seconds", [datalogLastTS](const Uploader &u) {
        return String(datalogLastTS > u.lastSentTS() ? datalogLastTS - u.lastSentTS() : 0);
    });
    head(out, "auramon_uploader_last_success_timestamp_seconds",
         "Unix timestamp of the last successful upload.", "gauge");
    uploaderSamples(out, "auramon_uploader_last_success_timestamp_seconds",
                    [](const Uploader &u) { return String((long) u.lastSuccessAt()); });
    head(out, "auramon_uploader_last_http_status",
         "HTTP status of the last completed request; -1 if the last attempt failed before a response was received, 0 if none has completed yet.",
         "gauge");
    uploaderSamples(out, "auramon_uploader_last_http_status",
                    [](const Uploader &u) { return String(u.lastHttpStatus()); });
    return out;
}
