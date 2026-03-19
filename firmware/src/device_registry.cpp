//
// DeviceRegistry method implementations.
//
// These methods are mutex-free; callers hold the appropriate lock.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif
#include "device_registry.h"

// syncInfos synchronizes from the control plane into the data plane.
// infoMu must be held while calling this function.
void DeviceRegistry::syncInfo() {
    for (size_t i = 0; i < MAX; i++) {
        if (infos[i] == nullptr) {
            if (devices[i] == nullptr) {
                continue;
            }
            delete devices[i];
            devices[i] = nullptr;
            continue;
        }

        if (devices[i] == nullptr) {
            devices[i] = new inputDevice(infos[i]->addr);
        }
        devices[i]->enabled     = infos[i]->enabled;
        devices[i]->name        = infos[i]->name;
        devices[i]->calibration = infos[i]->calibration;
        devices[i]->reversed    = infos[i]->reversed;
    }
}

// syncAction synchronizes the device action from the control plane to the data plane.
// actionMu must be held while calling this function.
void DeviceRegistry::syncAction() {
    if (actionControl.type == deviceActionType::None) {
        return;
    }
    actionData    = actionControl;
    actionControl = {deviceActionType::None, 0};
}

// syncData synchronizes device data from the data plane into the control plane.
// dataMu must be held while calling this function.
void DeviceRegistry::syncData() {
    for (size_t i = 0; i < MAX; i++) {
        if (devices[i] == nullptr) {
            if (data[i] != nullptr) {
                delete data[i];
                data[i] = nullptr;
            }
            continue;
        }

        if (data[i] == nullptr) {
            data[i] = new inputDeviceData{};
        }

        data[i]->name  = devices[i]->name;
        data[i]->volts = devices[i]->current.volts;
        data[i]->amps  = devices[i]->current.volts != 0.0
                             ? (devices[i]->current.va / devices[i]->current.volts)
                             : 0.0;
        data[i]->pf    = devices[i]->current.va != 0.0
                             ? devices[i]->current.watts / devices[i]->current.va
                             : 0.0;
        data[i]->hz    = devices[i]->current.hz;
    }
}

