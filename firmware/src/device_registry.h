//
// Created by Nicholas Wiersma on 2025/09/05.

#pragma once

#ifndef UNIT_TEST
#include <pico/mutex.h>
#endif

#include "device.h"

// DeviceRegistry owns the three-tier device state and their mutexes.
//
// Tier layout (dual-core RP2040):
//   infos[]   – Core 0 writes (config), Core 1 reads briefly during sync
//   devices[] – Core 1 only (collection working state, no external locking)
//   data[]    – Core 1 writes, Core 0 reads (published for API)
class DeviceRegistry {
public:
    static constexpr uint8_t MAX = 15;

    // Control plane devices (Core 0 writes, Core 1 reads)
    mutex_t           infoMu{};
    InputDeviceInfo * infos[MAX] = {};
    bool              changed = false;

    // Device actions (Core 0 writes, Core 1 reads)
    mutex_t             actionMu{};
    DeviceActionRequest actionControl{DeviceActionType::None, 0};
    DeviceActionRequest actionData{DeviceActionType::None, 0};

    // Sync device data (Core 0 reads, Core 1 writes)
    mutex_t           dataMu{};
    InputDeviceData * data[MAX] = {};

    // Data plane devices (Core 1 only)
    InputDevice * devices[MAX] = {};

    DeviceRegistry() {
        mutex_init(&infoMu);
        mutex_init(&actionMu);
        mutex_init(&dataMu);
    }

    void syncInfo();
    void syncAction();
    void syncData();
};
