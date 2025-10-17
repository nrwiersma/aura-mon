//
// Created by Nicholas Wiersma on 2025/09/20.
//

#include "auramon.h"
#include <task.h>

uint8_t readFrame(inputDevice *device);

float float_abcd(uint16_t hi, uint16_t lo);

void collectTask(void *param) {
    (void) param;

    while (true) {
        const unsigned long startTotal = millis();

        for (int i = 0; i < MAX_DEVICES; i++) {
            inputDevice *dev = devices[i];
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

        if (tookTotal < 1000) {
            vTaskDelay(pdMS_TO_TICKS(1000 - tookTotal));
        }
    }
}

uint8_t readFrame(inputDevice *device) {
    uint16_t data[10];
    if (const uint8_t err = modbus.readInputRegisters(device->addr, 0x4E20, data, 10)) {
        return err;
    }

    const float v = float_abcd(data[0], data[1]);
    float a = float_abcd(data[2], data[3]);
    const float pf = float_abcd(data[6], data[7]);
    const float hz = float_abcd(data[8], data[9]);

    double volts = v * device->calibration;
    if (device->reversed) {
        volts = -volts;
        a = -a;
    }
    const double va = volts * a;
    const double watts = va * pf;

    device->setEnergy(volts, watts, va, hz);

    return 0;
}

float float_abcd(uint16_t hi, uint16_t lo) {
    float f;
    uint32_t i;

    i = (static_cast<uint32_t>(hi) << 16) + lo;
    memcpy(&f, &i, sizeof(float));

    return f;
}
