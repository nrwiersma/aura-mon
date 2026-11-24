//
// Progress-gated hardware watchdog.
//

#pragma once

#include <stdint.h>

// The sources that must keep reporting progress for the watchdog to be fed.
enum class WdtSource : uint8_t {
    Collect,
    LogData,
    Count,
};

// Starts the hardware watchdog and the progress tracking.
void wdtBegin(uint32_t timeoutMs);

// Records that the source has made progress.
void wdtProgress(WdtSource source);

// Feeds the hardware watchdog, unless a source has stopped making progress
// or the watchdog is not being held. Safe to call from long running loops.
void wdtFeed();

// Suspends the progress checks for operations that legitimately block the
// main loop for a long time (uploads, OTA, file streaming). The hardware
// watchdog is still only fed by explicit wdtFeed calls, and a hold that is
// never released expires on its own.
void wdtHold();
void wdtRelease();

class WdtHoldGuard {
public:
    WdtHoldGuard() { wdtHold(); }
    ~WdtHoldGuard() { wdtRelease(); }

    WdtHoldGuard(const WdtHoldGuard &) = delete;
    WdtHoldGuard &operator=(const WdtHoldGuard &) = delete;
};
