//
// Created by Nicholas Wiersma on 2026/02/05.
//

#include "auramon.h"

uint32_t syncState(void *param) {
    (void) param;

    bool    sdPresent = false;
    uint8_t sdError   = 0;
    sd.with([&](auto &fs) {
        auto *c   = fs.card();
        sdPresent = c->status() != 0 && c->errorCode() == 0;
        sdError   = c->errorCode();
    });

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
