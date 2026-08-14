//
// Created by Nicholas Wiersma on 2026/02/05.
//

#include "auramon.h"

uint32_t syncState(void *param) {
    (void) param;

    uint8_t    sdError = 0;
    const bool sdPresent = storage::status(sdError);

    if (!sdPresent) {
        ledState = LEDColor::Red;

        LOGD("SD Card not present or error: %d", sdError);

        return 1000;
    }

    if (eth.linkStatus() != LinkON) {
        ledState = LEDColor::Orange;

        return 1000;
    }

    ledState = LEDColor::Green;

    return 1000;
}
