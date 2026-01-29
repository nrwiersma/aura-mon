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

    if (!mutex_enter_block_until(&deviceInfoMu, 100)) {
        LOGE("syncDevices: could not acquire deviceInfoMu");
        return 50;
    }
    if (!devicesChanged) {
        mutex_exit(&deviceInfoMu);
        return 900;
    }

    syncDeviceData();

    // Reset the changed flag.
    devicesChanged = false;

    mutex_exit(&deviceInfoMu);

    LOGD("Devices synchronized");

    return 900;
}
