//
// Created by Nicholas Wiersma on 2026/03/19.
//

#pragma once

#include <cstdint>

// restartReasonLog logs the reason for the last restart, if it was not abnormal.
void restartReasonLog();

// forceCore0HangReboot immediately forces a watchdog restart, leaving behind
// a breadcrumb of what core 0 appeared to be doing so restartReasonLog() can
// report it on the next boot.
//
// taskAddr is the address of the task function core 0 was last seen running
// (0 if unknown), and stuckMs is how long it had been running for.
[[noreturn]] void forceCore0HangReboot(uint32_t taskAddr, uint32_t stuckMs);
