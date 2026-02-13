//
// Created by Nicholas Wiersma on 2026/01/25.
//

#include "auramon.h"

void syncDeviceInfo() {
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

void syncDeviceAction() {
    if (deviceActionControl.type == deviceActionType::None) {
        return;
    }

    deviceActionData = deviceActionControl;
    deviceActionControl = {deviceActionType::None, 0};
}

void syncDeviceData() {
    for (uint32_t i = 0; i < MAX_DEVICES; i++) {
        if (devices[i] == nullptr) {
            if (deviceData[i] != nullptr) {
                delete deviceData[i];
                deviceData[i] = nullptr;
            }
            continue;
        }

        if (deviceData[i] == nullptr) {
            deviceData[i] = new inputDeviceData{};
        }

        deviceData[i]->name = devices[i]->name;
        deviceData[i]->volts = devices[i]->current.volts;
        deviceData[i]->amps = devices[i]->current.volts != 0.0
                                  ? (devices[i]->current.va / devices[i]->current.volts)
                                  : 0.0;
        deviceData[i]->pf = devices[i]->current.va != 0.0 ? devices[i]->current.watts / devices[i]->current.va : 0.0;
        deviceData[i]->hz = devices[i]->current.hz;
    }
}

uint32_t syncDevices(void *param) {
    (void) param;

    if (!mutex_enter_block_until(&deviceInfoMu, 100)) {
        LOGE("syncDevices: could not acquire deviceInfoMu");
        return 50;
    }
    if (devicesChanged) {
        syncDeviceInfo();

        // Reset the changed flag.
        devicesChanged = false;
    }

    mutex_exit(&deviceInfoMu);

    if (!mutex_enter_block_until(&deviceActionMu, 100)) {
        LOGE("syncDevices: could not acquire deviceActionMu");
        return 50;
    }

    syncDeviceAction();

    mutex_exit(&deviceActionMu);

    if (!mutex_enter_block_until(&deviceDataMu, 100)) {
        LOGE("syncDevices: could not acquire deviceDataMu");
        return 50;
    }

    syncDeviceData();

    mutex_exit(&deviceDataMu);

    return 1000;
}
