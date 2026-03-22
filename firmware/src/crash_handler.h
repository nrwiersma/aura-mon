//
// Created by Nicholas Wiersma on 2026/03/19.
//

#pragma once

// Sentinel written to scratch[0] so the firmware can detect a crash reboot.
static constexpr uint32_t CRASH_MAGIC = 0xDEADC0DEu;

// crashDetected returns true if the hard fault handler was entered.
inline bool crashDetected() {
    return watchdog_hw->scratch[0] == CRASH_MAGIC;}

// restartReasonLog logs the reason for the last restart, if it was not abnormal.
void restartReasonLog();
