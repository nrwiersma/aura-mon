//
// Created by Nicholas Wiersma on 2025/09/20.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "device.h"
#include "modbus.h"
#endif

uint8_t readFrame(InputDevice *device);
float   floatAbcd(uint16_t hi, uint16_t lo);

void collect() {
    const unsigned long startTotal = millis();
    uint32_t            deviceCount = 0;
    uint64_t            deviceTimeMs = 0;

    metrics.modbus_collect_runs_total.fetch_add(1, std::memory_order_relaxed);

    for (const auto dev : devices) {
        if (!dev || !dev->isEnabled()) {
            continue;
        }

        const unsigned long start = millis();
        if (!dev->shouldPoll(start)) {
            continue;
        }

        const bool wasOffline = dev->isOffline();

        if (const uint8_t err = readFrame(dev); err) {
            // Failed probes of an offline device are expected and not worth reporting.
            if (!wasOffline) {
                metrics.modbus_errors_total.fetch_add(1, std::memory_order_relaxed);
                metrics.modbus_device_errors_total[dev->addr - 1].fetch_add(1, std::memory_order_relaxed);
                LOGE("Could not read data from device %d: %s", dev->addr, modbusError(err));
            }

            if (dev->recordFailure(start)) {
                LOGE("Device %d is offline after %d failed reads", dev->addr, DEVICE_OFFLINE_FAILURES);

                // Stop the last reading being integrated over the outage. Frequency is kept so
                // the averaged frequency is not dragged down.
                dev->setEnergy(0, 0, 0, dev->current.hz);
            }

            wdtFeed();

            continue;
        }

        if (dev->recordSuccess()) {
            LOGI("Device %d is back online", dev->addr);
        }

        deviceCount++;
        const unsigned long took = millis() - start;
        deviceTimeMs += took;
        metrics.modbus_device_last_collect_ms[dev->addr - 1].store(static_cast<uint32_t>(took), std::memory_order_relaxed);

        Bucket curr = dev->current;
        LOGD("%d: %.0fV %.3fW %.2fVA %.2fHz in %dms", dev->addr, curr.volts, curr.watts, curr.va, curr.hz, took);

        wdtFeed();
    }

    const unsigned long tookTotal = millis() - startTotal;
    LOGD("Collecting data took %dms", tookTotal);

    metrics.modbus_collect_time_ms_total.fetch_add(tookTotal, std::memory_order_relaxed);
    const uint32_t avgMs = deviceCount > 0 ? static_cast<uint32_t>(deviceTimeMs / deviceCount) : 0;
    metrics.modbus_last_run_avg_ms.store(avgMs, std::memory_order_relaxed);

    wdtProgress(WdtSource::Collect);
}

#ifndef UNIT_TEST
// Masks interrupts for the duration of a modbus transaction. The RTU library
// polls the UART and treats a gap of over ~1ms between bytes as the end of the
// frame, so any interrupt servicing in the middle of a response is read as a
// frame error. A transaction is ~10ms, and the response fits in the UART FIFO.
class IrqGuard {
public:
    IrqGuard() : _state(save_and_disable_interrupts()) {}
    ~IrqGuard() { restore_interrupts(_state); }

    IrqGuard(const IrqGuard &) = delete;
    IrqGuard &operator=(const IrqGuard &) = delete;

private:
    uint32_t _state;
};
#else
struct IrqGuard {};
#endif

uint8_t readFrame(InputDevice *device) {
    uint16_t data[10];
    uint8_t  err;
    {
        IrqGuard guard;
        err = modbus.readInputRegisters(device->addr, 0x4E20, data, 10);
    }
    if (err) {
        return err;
    }

    float v = floatAbcd(data[0], data[1]);
    float a = floatAbcd(data[2], data[3]);
    float pf = floatAbcd(data[6], data[7]);
    float hz = floatAbcd(data[8], data[9]);

    double volts = v * device->calibration;
    if (device->reversed) {
        volts = -volts;
        a = -a;
    }
    double va = volts * a;
    double watts = va * pf;

    device->setEnergy(volts, watts, va, hz);

    return 0;
}

float floatAbcd(uint16_t hi, uint16_t lo) {
    float    f;
    uint32_t i;

    i = ((uint32_t) hi << 16) + lo;
    memcpy(&f, &i, sizeof(float));

    return f;
}
