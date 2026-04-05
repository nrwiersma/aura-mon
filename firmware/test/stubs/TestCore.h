//
// Globals and stubs required by production code under native tests.
// This is the single comprehensive stub header; all test suites include it.
//

#pragma once

#include "TestPlatform.h"
#include "TestLWIP.h"
#include "TestSdFat.h"
#include "../../src/device.h"
#include "../../src/ethernet.h"
#include "../../src/metrics.h"
#include <errors.h>

// ---- constants that auramon.h normally provides ----------------------------
#define MAX_DEVICES         15
#define MS_PER_HOUR         3600000UL
#define DATA_LOG_PATH       "aura-mon/data.log"
#define CONFIG_LOG_PATH     "aura-mon/config.json"
#define CONFIG_LOG_TMP_PATH "aura-mon/config.json.tmp"

// ---- logging macros (silent during tests) ----------------------------------
#define LOGD(...)
#define LOGI(...)
#define LOGE(...)

// ---- globals ---------------------------------------------------------------
inline mutex_t deviceInfoMu;
inline volatile bool devicesChanged = false;
inline InputDeviceInfo *deviceInfos[MAX_DEVICES] = {};

inline mutex_t deviceMu;
inline InputDevice *devices[MAX_DEVICES] = {};

inline mutex_t deviceDataMu;
inline InputDeviceData *deviceData[MAX_DEVICES] = {};

inline mutex_t deviceActionMu;
inline DeviceActionRequest deviceActionControl = {DeviceActionType::None, 0};
inline DeviceActionRequest deviceActionData    = {DeviceActionType::None, 0};

inline NetworkConfig netCfg;
inline promMetrics   metrics;

// ---- ModbusRTUMaster stub (used by collect.cpp) ----------------------------
struct ModbusRTUMaster {
    uint8_t readInputRegisters(uint8_t /*id*/, uint16_t /*addr*/,
                               uint16_t * /*buf*/, uint16_t /*qty*/) { return 0; }
    uint8_t getExceptionResponse() { return 0; }
};
inline ModbusRTUMaster modbus;
