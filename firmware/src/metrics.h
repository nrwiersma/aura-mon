//
// Created by Nicholas Wiersma on 2026/02/10.
//

#pragma once

#include <atomic>

struct promMetrics {
    // Modbus / collection
    std::atomic<uint32_t> modbus_errors_total{0};
    std::atomic<uint64_t> modbus_collect_time_ms_total{0};
    std::atomic<uint32_t> modbus_last_run_avg_ms{0};
    std::atomic<uint32_t> modbus_collect_runs_total{0};
    std::atomic<uint32_t> modbus_device_errors_total[15];
    std::atomic<uint32_t> modbus_device_last_collect_ms[15];

    // Datalog
    std::atomic<uint32_t> datalog_read_io{0};
    std::atomic<uint32_t> datalog_write_io{0};
    std::atomic<uint32_t> datalog_write_time_ms_total{0};
    std::atomic<uint32_t> datalog_cache_hit{0};
    std::atomic<uint32_t> datalog_write_errors_total{0};
    std::atomic<uint32_t> datalog_queue_depth{0};
    std::atomic<uint32_t> datalog_queue_full_total{0};
    std::atomic<uint32_t> datalog_records_dropped_total{0};

    // NTP
    std::atomic<uint32_t> ntp_syncs_total{0};
    std::atomic<uint32_t> ntp_failures_total{0};
    std::atomic<int32_t>  ntp_last_offset_ms{0};

    // Network
    std::atomic<uint32_t> ethernet_disconnects_total{0};

    // Logging
    std::atomic<uint32_t> log_errors_total{0};
};
