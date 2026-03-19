//
// Created by Nicholas Wiersma on 2026/02/12.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#include "config.h"
#endif

uint8_t findAvailableAddressLocked() {
    bool used[MAX_DEVICES + 1] = {};
    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        if (!registry.infos[i]) {
            continue;
        }
        uint8_t addr = registry.infos[i]->addr;
        if (addr >= 1 && addr <= MAX_DEVICES) {
            used[addr] = true;
        }
    }

    for (uint8_t addr = 1; addr <= MAX_DEVICES; addr++) {
        if (!used[addr] && registry.infos[addr - 1] == nullptr) {
            return addr;
        }
    }

    return 0;
}

uint32_t addDeviceFromButton(void *param) {
    (void) param;

    uint8_t address = 0;

    mutex_enter_blocking(&registry.infoMu);
    address = findAvailableAddressLocked();
    if (address == 0) {
        mutex_exit(&registry.infoMu);
        LOGI("No free device slots available");
        return 0;
    }

    size_t idx = static_cast<size_t>(address - 1);
    if (registry.infos[idx] != nullptr) {
        mutex_exit(&registry.infoMu);
        LOGI("Device slot already in use for address %u", address);
        return 0;
    }

    auto info = new inputDeviceInfo(address);
    info->enabled = true;
    char nameBuf[24];
    if (snprintf(nameBuf, sizeof(nameBuf), "Device %u", address) > 0) {
        info->name = nameBuf;
    } else {
        info->name = "Device";
    }
    registry.infos[idx] = info;
    registry.changed = true;

    mutex_exit(&registry.infoMu);

    if (auto err = saveConfig(); err) {
        LOGE("Failed to save config after button add: %s", err->Error());
    }

    if (!mutex_enter_block_until(&registry.actionMu, 100)) {
        LOGE("Button add: could not acquire actionMu");
        return 0;
    }

    registry.actionControl = {deviceActionType::Assign, address};
    mutex_exit(&registry.actionMu);

    return 0;
}
