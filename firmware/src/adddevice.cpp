//
// Created by Nicholas Wiersma on 2026/02/12.
//

#include "auramon.h"

uint8_t findAvailableAddressLocked() {
    bool used[MAX_DEVICES + 1] = {};
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        if (!deviceInfos[i]) {
            continue;
        }
        uint8_t addr = deviceInfos[i]->addr;
        if (addr >= 1 && addr <= MAX_DEVICES) {
            used[addr] = true;
        }
    }

    for (uint8_t addr = 1; addr <= MAX_DEVICES; addr++) {
        if (!used[addr] && deviceInfos[addr - 1] == nullptr) {
            return addr;
        }
    }

    return 0;
}

uint32_t addDeviceFromButton(void *param) {
    (void) param;

    uint8_t address = 0;

    mutex_enter_blocking(&deviceInfoMu);
    address = findAvailableAddressLocked();
    if (address == 0) {
        mutex_exit(&deviceInfoMu);
        LOGI("No free device slots available");
        return 0;
    }

    size_t idx = static_cast<size_t>(address - 1);
    if (deviceInfos[idx] != nullptr) {
        mutex_exit(&deviceInfoMu);
        LOGI("Device slot already in use for address %u", address);
        return 0;
    }

    auto info = new inputDeviceInfo(address);
    info->enabled = true;
    char nameBuf[24];
    if (snprintf(nameBuf, sizeof(nameBuf), "Device %u", address) > 0) {
        info->name = strdup(nameBuf);
    } else {
        info->name = strdup("Device");
    }
    deviceInfos[idx] = info;
    devicesChanged = true;

    mutex_exit(&deviceInfoMu);

    if (auto err = saveConfig(); err) {
        LOGE("Failed to save config after button add: %s", err->Error());
    }

    if (!mutex_enter_block_until(&deviceActionMu, 100)) {
        LOGE("Button add: could not acquire deviceActionMu");
        return 0;
    }

    deviceActionControl = {deviceActionType::Assign, address};
    mutex_exit(&deviceActionMu);

    return 0;
}
