//
// Created by Nicholas Wiersma on 2026/02/05.
//

#include "auramon.h"

uint32_t syncState(void *param) {
    (void) param;

    auto writeFailures = metrics.datalog_write_consecutive_failures.load(std::memory_order_relaxed);

    if (writeFailures > 1) {
        ledState = LEDColor::Red;

        LOGD("Not able to write to SD Card: %d", writeFailures);

        return 1000;
    }

    if (eth.linkStatus() != LinkON) {
        ledState = LEDColor::Orange;

        return 1000;
    }

    ledState = LEDColor::Green;

    return 1000;
}
