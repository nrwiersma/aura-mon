//
// Progress-gated hardware watchdog.
//

#include "auramon.h"

namespace {
    // How long a source may go without progress before the watchdog is no
    // longer fed.
    constexpr uint32_t PROGRESS_TIMEOUT_MS = 60000;

    // The longest a hold suspends the progress checks, so a leaked hold
    // cannot mask a hang forever.
    constexpr uint32_t HOLD_MAX_MS = 600000;

    constexpr size_t SOURCE_COUNT = static_cast<size_t>(WdtSource::Count);

    const char *const SOURCE_NAMES[SOURCE_COUNT] = {"collect", "logdata"};

    uint32_t lastProgress[SOURCE_COUNT] = {};
    uint8_t  holds = 0;
    uint32_t holdSince = 0;
    bool     stallLogged = false;

    void resetProgress() {
        const uint32_t now = millis();
        for (auto &p: lastProgress) {
            p = now;
        }
        stallLogged = false;
    }

    bool healthy() {
        const uint32_t now = millis();

        if (holds > 0 && now - holdSince < HOLD_MAX_MS) {
            return true;
        }

        for (size_t i = 0; i < SOURCE_COUNT; i++) {
            if (now - lastProgress[i] <= PROGRESS_TIMEOUT_MS) {
                continue;
            }

            if (!stallLogged) {
                stallLogged = true;
                LOGE("Watchdog: no %s progress for %" PRIu32 "ms. Restarting", SOURCE_NAMES[i], now - lastProgress[i]);
            }
            return false;
        }

        return true;
    }
}

void wdtBegin(uint32_t timeoutMs) {
    resetProgress();
    rp2040.wdt_begin(timeoutMs);
}

void wdtProgress(WdtSource source) {
    lastProgress[static_cast<size_t>(source)] = millis();
}

void wdtFeed() {
    if (!healthy()) {
        return;
    }
    rp2040.wdt_reset();
}

void wdtHold() {
    if (holds == 0) {
        holdSince = millis();
    }
    if (holds < UINT8_MAX) {
        holds++;
    }
}

void wdtRelease() {
    if (holds == 0) {
        return;
    }
    holds--;
    if (holds == 0) {
        // Give the main loop a full window to catch up after the hold.
        resetProgress();
    }
}
