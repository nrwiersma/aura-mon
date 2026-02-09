//
// Created by Nicholas Wiersma on 2026/02/09.
//

#include "auramon.h"

uint32_t deviceActionTask(void *param) {
    (void) param;

    deviceActionRequest action{deviceActionType::None, 0};
    bool                hasAction = false;

    if (!mutex_enter_block_until(&deviceActionMu, 100)) {
        LOGE("deviceActionTask: could not acquire deviceActionMu");
        return 200;
    }

    if (deviceActionData.type == deviceActionType::None) {
        mutex_exit(&deviceActionMu);
        return 1000;
    }

    action = deviceActionData;
    deviceActionData = {deviceActionType::None, 0};
    hasAction = true;

    mutex_exit(&deviceActionMu);

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
