//
// Created by Nicholas Wiersma on 2025/09/26.
//

#pragma once

struct Bucket {
    double   volts;
    double   watts;
    double   va;
    double   hz;
    double   voltHrs;
    double   wattHrs;
    double   vaHrs;
    double   hzHrs;
    uint32_t ts;

    Bucket() : volts(0), watts(0), va(0), hz(0),
               voltHrs(0), wattHrs(0), vaHrs(0), hzHrs(0),
               ts(millis()) {
    }
};

class InputDeviceInfo {
public:
    bool    enabled;
    uint8_t addr;
    String  name;
    float   calibration;
    bool    reversed;

    InputDeviceInfo(uint8_t addr)
        : enabled(false),
          addr(addr),
          calibration(1.0f),
          reversed(false) {
    }

    bool isEnabled() const { return enabled; }
};

class InputDevice : public InputDeviceInfo {
public:
    Bucket current;

    InputDevice(uint8_t addr) : InputDeviceInfo(addr) {
    }

    ~InputDevice() = default;
    void reset();
    void accumulate(uint32_t now);
    void setEnergy(double volts, double watts, double va, double hz);
};

struct InputDeviceData {
    String name;
    double volts;
    double amps;
    double pf;
    double hz;
};

enum class DeviceActionType : uint8_t { None = 0, Locate, Assign };

struct DeviceActionRequest {
    DeviceActionType type;
    uint8_t          address;
};
