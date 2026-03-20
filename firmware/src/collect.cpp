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

    for (const auto dev : registry.devices) {
        if (!dev || !dev->isEnabled()) {
            continue;
        }

        const unsigned long start = millis();

        if (const uint8_t err = readFrame(dev); err) {
            metrics.modbus_errors_total.fetch_add(1, std::memory_order_relaxed);
            LOGE("Could not read data from device %d: %s", dev->addr, modbusError(err));

            continue;
        }

        deviceCount++;
        const unsigned long took = millis() - start;
        deviceTimeMs += took;

        Bucket curr = dev->current;
        LOGD("%d: %.0fV %.3fW %.2fVA %.2fHz in %dms", dev->addr, curr.volts, curr.watts, curr.va, curr.hz, took);

        rp2040.wdt_reset();
    }

    const unsigned long tookTotal = millis() - startTotal;
    LOGD("Collecting data took %dms", tookTotal);

    metrics.modbus_collect_time_ms_total.fetch_add(tookTotal, std::memory_order_relaxed);
    const uint32_t avgMs = deviceCount > 0 ? static_cast<uint32_t>(deviceTimeMs / deviceCount) : 0;
    metrics.modbus_last_run_avg_ms.store(avgMs, std::memory_order_relaxed);
}

uint8_t readFrame(InputDevice *device) {
    uint16_t data[10];
    if (const uint8_t err = modbus.readInputRegisters(device->addr, 0x4E20, data, 10)) {
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
