//
// Created by Nicholas Wiersma on 2026/02/09.
//

#include "auramon.h"

uint32_t deviceActionTask(void *param) {
    (void) param;

    DeviceActionRequest action{DeviceActionType::None, 0};

    if (!mutex_enter_timeout_ms(&deviceActionMu, 100)) {
        LOGE("deviceActionTask: could not acquire deviceActionMu");
        return 200;
    }

    if (deviceActionData.type == DeviceActionType::None) {
        mutex_exit(&deviceActionMu);
        return 1000;
    }

    action = deviceActionData;
    deviceActionData = {DeviceActionType::None, 0};

    mutex_exit(&deviceActionMu);

    switch (action.type) {
        case DeviceActionType::Locate:
            locateModbusDevice(action.address);
            break;
        case DeviceActionType::Assign:
            assignModbusAddress(action.address);
            break;
        default:
            break;
    }

    return 1000;
}
