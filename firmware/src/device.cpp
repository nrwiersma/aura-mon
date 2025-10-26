//
// Created by Nicholas Wiersma on 2025/09/26.
//

#include "auramon.h"

void inputDevice::reset() {
    enabled = false;
    delete[] name;
    name = nullptr;
    calibration = 0;
    reversed = false;
}

void inputDevice::accumulate(uint32_t now) {
    if (now <= current.ts) {
        return;
    }
    const double hrs = static_cast<double>(now - current.ts) / MS_PER_HOUR;
    current.voltHrs += current.volts * hrs;
    current.wattHrs += current.watts * hrs;
    current.vaHrs += current.va * hrs;
    current.hzHrs += current.hz * hrs;
    current.ts = now;
}

void inputDevice::setEnergy(double volts, double watts, double va, double hz) {
    current.volts = volts;
    current.watts = watts;
    current.va = va;
    current.hz = hz;
    accumulate(millis());
}
