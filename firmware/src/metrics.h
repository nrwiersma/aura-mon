//
// Created by Nicholas Wiersma on 2026/02/10.
//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Upper bounds in milliseconds of the datalog write duration buckets; a final
// implicit bucket holds everything above the last bound.
constexpr uint32_t datalogWriteBucketsMs[] = {10, 25, 50, 100, 250, 500, 1000, 2500};
constexpr size_t   datalogWriteBucketCount = sizeof(datalogWriteBucketsMs) / sizeof(datalogWriteBucketsMs[0]);

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
    // Non-cumulative counts, one per bound plus a trailing overflow bucket.
    std::atomic<uint32_t> datalog_write_time_buckets[datalogWriteBucketCount + 1]{};
    std::atomic<uint32_t> datalog_cache_hit{0};
    std::atomic<uint32_t> datalog_write_errors_total{0};
    std::atomic<uint32_t> datalog_write_consecutive_failures{0};

    // observeDatalogWrite records the duration of a successful datalog write.
    // It is only called once per log interval, so it stays cheap and lock-free.
    void observeDatalogWrite(uint32_t ms) {
        size_t i = 0;
        while (i < datalogWriteBucketCount && ms > datalogWriteBucketsMs[i]) {
            i++;
        }
        datalog_write_time_buckets[i].fetch_add(1, std::memory_order_relaxed);
        datalog_write_time_ms_total.fetch_add(ms, std::memory_order_relaxed);
    }

    // NTP
    std::atomic<uint32_t> ntp_syncs_total{0};
    std::atomic<uint32_t> ntp_failures_total{0};
    std::atomic<int32_t>  ntp_last_offset_ms{0};

    // Network
    std::atomic<uint32_t> ethernet_disconnects_total{0};

    // Logging
    std::atomic<uint32_t> log_errors_total{0};
};
