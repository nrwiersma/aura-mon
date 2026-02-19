//
// Created by Nicholas Wiersma on 2026/02/10.
//

#ifndef FIRMWARE_METRICS_H
#define FIRMWARE_METRICS_H

#include <atomic>

struct promMetrics {
    std::atomic<uint32_t> modbus_errors_total{0};
    std::atomic<uint64_t> modbus_collect_time_ms_total{0};
    std::atomic<uint32_t> modbus_last_run_avg_ms{0};
    std::atomic<uint32_t> datalog_io{0};
    std::atomic<uint32_t> datalog_cache_hit{0};
};

#endif //FIRMWARE_METRICS_H
