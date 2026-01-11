//
// Created by Nicholas Wiersma on 2025/09/20.
//

#include "auramon.h"

uint8_t readFrame(inputDevice *device);
float   float_abcd(uint16_t hi, uint16_t lo);

void collect() {
    const unsigned long startTotal = millis();

    for (const auto dev : devices) {
        if (!dev || !dev->isEnabled()) {
            continue;
        }

        const unsigned long start = millis();

        if (const uint8_t err = readFrame(dev); err) {
            // TODO: Report the error.
            LOGE("Could not read data from device %d: %s", dev->addr, modbusError(err));

            continue;
        }

        const unsigned long took = millis() - start;

        bucket curr = dev->current;
        LOGD("%d: %.0fV %.3fW %.2fVA %.2fHz in %dms", dev->addr, curr.volts, curr.watts, curr.va, curr.hz, took);
    }

    const unsigned long tookTotal = millis() - startTotal;
    LOGD("Collecting data took %dms", tookTotal);

    // TODO: collect stats about collection times.
}

uint8_t readFrame(inputDevice *device) {
    uint16_t data[10];
    if (const uint8_t err = modbus.readInputRegisters(device->addr, 0x4E20, data, 10)) {
        return err;
    }

    float v = float_abcd(data[0], data[1]);
    float a = float_abcd(data[2], data[3]);
    float pf = float_abcd(data[6], data[7]);
    float hz = float_abcd(data[8], data[9]);

    double volts = v * device->calibration;
    if (device->reversed) {
        volts = -volts;
        a = -a;
    }
    double va = volts * a;
    double watts = va * pf;

    mutex_enter_blocking(&devicesMu);
    device->setEnergy(volts, watts, va, hz);
    mutex_exit(&devicesMu);

    return 0;
}

float float_abcd(uint16_t hi, uint16_t lo) {
    float    f;
    uint32_t i;

    i = ((uint32_t) hi << 16) + lo;
    memcpy(&f, &i, sizeof(float));

    return f;
}
