//
// Created by Nicholas Wiersma on 2025/09/26.
//

#ifndef FIRMWARE_CHANNEL_H
#define FIRMWARE_CHANNEL_H

struct bucket {
    double   volts;
    double   watts;
    double   va;
    double   hz;
    double   voltHrs;
    double   wattHrs;
    double   vaHrs;
    double   hzHrs;
    uint32_t ts;

    bucket() : volts(0), watts(0), va(0), hz(0),
               voltHrs(0), wattHrs(0), vaHrs(0), hzHrs(0),
               ts(millis()) {
    }
};

class inputDeviceInfo {
public:
    bool        enabled;
    uint8_t     addr;
    const char *name;
    float       calibration;
    bool        reversed;

    inputDeviceInfo(uint8_t addr)
        : enabled(false),
          addr(addr),
          name(nullptr),
          calibration(1.0f),
          reversed(false) {
    }

    bool isEnabled() const { return enabled; }
};

class inputDevice : public inputDeviceInfo {
public:
    bucket current;

    inputDevice(uint8_t addr) : inputDeviceInfo(addr) {
    }

    ~inputDevice() = default;
    void reset();
    void accumulate(uint32_t now);
    void setEnergy(double volts, double watts, double va, double hz);
};

struct inputDeviceData {
    const char *name;
    double volts;
    double amps;
    double pf;
    double hz;
};

#endif //FIRMWARE_CHANNEL_H
