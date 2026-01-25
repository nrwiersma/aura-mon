//
// Created by Nicholas Wiersma on 2026/01/25.
//

#include "auramon.h"

void syncDeviceData() {
    for (uint32_t i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i] == nullptr) {
            if (devices[i] == nullptr) {
                continue;
            }

            // Remove device
            delete devices[i];
            devices[i] = nullptr;
            continue;
        }

        if (deviceInfos[i] != nullptr) {
            if (devices[i] == nullptr) {
                devices[i] = new inputDevice(deviceInfos[i]->addr);
            }
            devices[i]->enabled = deviceInfos[i]->enabled;
            devices[i]->name = deviceInfos[i]->name;
            devices[i]->calibration = deviceInfos[i]->calibration;
            devices[i]->reversed = deviceInfos[i]->reversed;
        }
    }
}

uint32_t syncDevices(void *param) {
    (void) param;

    mutex_enter_timeout_ms(&deviceInfoMu, 100);
    if (!devicesChanged) {
        return 900;
    }

    syncDeviceData();

    // Reset the changed flag.
    devicesChanged = false;

    mutex_enter_blocking(&deviceInfoMu);

    LOGD("Devices synchronized");

    return 900;
}
