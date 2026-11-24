//
// Created by Nicholas Wiersma on 2026/02/12.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "config.h"
#endif

uint8_t findAvailableAddress() {
    bool used[MAX_DEVICES + 1] = {};
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i]) {
            continue;
        }
        uint8_t addr = devices[i]->addr;
        if (addr >= 1 && addr <= MAX_DEVICES) {
            used[addr] = true;
        }
    }

    for (uint8_t addr = 1; addr <= MAX_DEVICES; addr++) {
        if (!used[addr] && devices[addr - 1] == nullptr) {
            return addr;
        }
    }

    return 0;
}

uint32_t addDeviceFromButton(void *param) {
    (void) param;

    uint8_t address = 0;

    address = findAvailableAddress();
    if (address == 0) {
        LOGI("No free device slots available");
        return 0;
    }

    size_t idx = static_cast<size_t>(address - 1);
    if (devices[idx] != nullptr) {
        LOGI("Device slot already in use for address %u", address);
        return 0;
    }

    auto device = new InputDevice(address);
    device->enabled = true;
    char nameBuf[24];
    if (snprintf(nameBuf, sizeof(nameBuf), "Device %u", address) > 0) {
        device->name = nameBuf;
    } else {
        device->name = "Device";
    }
    devices[idx] = device;

    if (auto err = saveConfig(); err) {
        LOGE("Failed to save config after button add: %s", err.Error());
    }

    deviceAction = {DeviceActionType::Assign, address};
    return 0;
}
