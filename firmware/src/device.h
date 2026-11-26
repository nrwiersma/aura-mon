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

// A tripped breaker cuts power to its sensor, which cannot be told apart from a
// broken sensor. A device that keeps failing is considered offline and is only
// probed occasionally until it responds again.
constexpr uint8_t  DEVICE_OFFLINE_FAILURES = 5;
constexpr uint32_t DEVICE_PROBE_INTERVAL_MS = 60000;

class InputDevice {
public:
    bool    enabled;
    uint8_t addr;
    String  name;
    float   calibration;
    bool    reversed;
    Bucket current;

    InputDevice(uint8_t addr)
        : enabled(false),
          addr(addr),
          calibration(1.0f),
          reversed(false),
          _offline(false),
          _failures(0),
          _lastAttempt(0) {
    }

    bool isEnabled() const { return enabled; }
    bool isOffline() const { return _offline; }

    // An offline device is only polled once per probe interval.
    bool shouldPoll(uint32_t now) const;

    // Returns true if this failure took the device offline.
    bool recordFailure(uint32_t now);

    // Returns true if the device was offline.
    bool recordSuccess();

    void reset();
    void accumulate(uint32_t now);
    void setEnergy(double volts, double watts, double va, double hz);

private:
    bool     _offline;
    uint8_t  _failures;
    uint32_t _lastAttempt;
};

enum class DeviceActionType : uint8_t { None = 0, Locate, Assign };

struct DeviceActionRequest {
    DeviceActionType type;
    uint8_t          address;
};
