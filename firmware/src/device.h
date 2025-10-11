//
// Created by Nicholas Wiersma on 2025/09/26.
//

#ifndef FIRMWARE_CHANNEL_H
#define FIRMWARE_CHANNEL_H
#include <cstdint>

struct bucket {
    double volts;
    double watts;
    double va;
    double hz;
    double voltHrs;
    double wattHrs;
    double vaHrs;
    double hzHrs;
    uint32_t ts;
};

class inputDevice {
public:
    bool enabled;
    uint8_t addr;
    char *name;
    float calibration;
    bool reversed;
    bucket current;

    inputDevice(uint8_t addr)
    : enabled(false),
    addr(addr),
    name(nullptr),
    calibration(1.0f),
    reversed(false)
    {}

    ~inputDevice() {}

    bool isEnabled() const { return enabled; }
    void reset();
    void accumulate(uint32_t now);
    void setEnergy(double volts, double watts, double va, double hz);
};

#endif //FIRMWARE_CHANNEL_H
