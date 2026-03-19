//
// Created by Nicholas Wiersma on 2026/02/09.
//

#include "auramon.h"

uint32_t deviceActionTask(void *param) {
    (void) param;

    deviceActionRequest action{deviceActionType::None, 0};

    if (!mutex_enter_block_until(&registry.actionMu, 100)) {
        LOGE("deviceActionTask: could not acquire actionMu");
        return 200;
    }

    if (registry.actionData.type == deviceActionType::None) {
        mutex_exit(&registry.actionMu);
        return 1000;
    }

    action = registry.actionData;
    registry.actionData = {deviceActionType::None, 0};

    mutex_exit(&registry.actionMu);

    switch (action.type) {
        case deviceActionType::Locate:
            locateModbusDevice(action.address);
            break;
        case deviceActionType::Assign:
            assignModbusAddress(action.address);
            break;
        default:
            break;
    }

    return 1000;
}
