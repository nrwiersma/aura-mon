//
// Created by Nicholas Wiersma on 2026/02/09.
//

#include "auramon.h"

uint32_t deviceActionTask(void *param) {
    (void) param;

    DeviceActionRequest action{DeviceActionType::None, 0};

    if (deviceAction.type == DeviceActionType::None) {
        return 1000;
    }

    action = deviceAction;
    deviceAction = {DeviceActionType::None, 0};

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
