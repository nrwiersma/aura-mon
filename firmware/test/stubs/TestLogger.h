//
// Minimal globals required to compile logger.cpp under native tests.
//

#pragma once

#include <ctime>
#include "TestPlatform.h"
#include "TestSdFat.h"

// Path used by logger when writing the message log.
#define MESSAGE_LOG_PATH "aura-mon/log.txt"

// Redirect time() calls in logger.cpp to our controllable stub.
inline time_t mockTime(time_t *t) {
    if (t) *t = mockNow;
    return mockNow;
}
#define time mockTime

// logger.cpp includes auramon.h which pulls in everything; for logger-only
// tests we instead provide only the symbols logger.cpp actually references.
// The mutex and sd globals are already provided by TestSdFat.h / TestPlatform.h.
#include "../../src/metrics.h"
inline promMetrics metrics;

