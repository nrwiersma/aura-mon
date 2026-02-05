//
// Created by Nicholas Wiersma on 2026/02/05.
//

#include "auramon.h"

uint32_t syncState(void *param) {
    (void) param;

    mutex_enter_blocking(&sdMu);
    auto sdPresent = sd.card()->status() != 0 && sd.card()->errorCode() == 0;
    if (!sdPresent) {
        ledState = LEDColor::Red;

        LOGD("SD Card not present or error: %d", sd.card()->errorCode());

        mutex_exit(&sdMu);
        return 1000;
    }
    mutex_exit(&sdMu);

    if (eth.linkStatus() != LinkON) {
        ledState = LEDColor::Orange;

        return 1000;
    }

    ledState = LEDColor::Green;

    return 1000;
}
