//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include "TestPlatform.h"
#include "TestLWIP.h"
#include "TestSdFat.h"
#include "../../src/device.h"
#include "../../src/ethernet.h"
#include "../../src/metrics.h"

// Mock the constants and globals needed
#define DATA_LOG_PATH "aura-mon/data.log"
#define CONFIG_LOG_PATH "aura-mon/config.json"
#define MS_PER_HOUR 3600000UL

// Mock logging macros
#define LOGD(...)
#define LOGE(...)

#define MAX_DEVICES 15
inline mutex_t deviceInfoMu;
inline inputDeviceInfo *deviceInfos[MAX_DEVICES] = {};

inline NetworkConfig netCfg;

inline promMetrics metrics;
