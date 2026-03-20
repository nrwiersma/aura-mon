//
// Created by Nicholas Wiersma on 2026/01/25.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif

uint32_t syncDevices(void *param) {
    (void) param;

    if (!mutex_enter_block_until(&registry.infoMu, 100)) {
        LOGE("syncDevices: could not acquire infoMu");
        return 50;
    }
    if (registry.changed) {
        registry.syncInfo();
        registry.changed = false;
    }
    mutex_exit(&registry.infoMu);

    if (!mutex_enter_block_until(&registry.actionMu, 100)) {
        LOGE("syncDevices: could not acquire actionMu");
        return 50;
    }
    registry.syncAction();
    mutex_exit(&registry.actionMu);

    if (!mutex_enter_block_until(&registry.dataMu, 100)) {
        LOGE("syncDevices: could not acquire dataMu");
        return 50;
    }
    registry.syncData();
    mutex_exit(&registry.dataMu);

    return 1000;
}
