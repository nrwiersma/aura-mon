//
// Created by Nicholas Wiersma on 2025/09/05.
//

#ifndef FIRMWARE_AURAMON_H
#define FIRMWARE_AURAMON_H

#include <Arduino.h>

#include <W5500lwIP.h>
#include <SdFat.h>
#include <pico/mutex.h>

#include <ModbusRTUMaster.h>

#include <Wire.h>
#include <RTClib.h>

#include "log.h"
#include "logger.h"
#include "modbus.h"
#include "device.h"
#include "collect.h"

#define DEBUG 1

#define MESSAGE_LOG_PATH "/aura-mon/log.txt"

#define ETH_INT  20

#define SD_CLK 4
#define SD_CMD 5
#define SD_DAT0 6 // DAT[1:3] - 7:9
#define SD_CONFIG SdioConfig(SD_CLK, SD_CMD, SD_DAT0)

#define RS485_TX 0
#define RS485_RX 1
#define RS485_DE 2
#define RS485_BAUDRATE 38400

inline byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE};
inline char hostname[] = "aura-mon";

extern Wiznet5500lwIP eth;

extern RTC_PCF8563 rtc;

extern mutex_t sdMu;
extern SdFs sd;

extern ModbusRTUMaster modbus;

#define MAX_DEVICES 20
extern inputDevice* *devices;

extern logger msgLog;
#define error(format,...) msgLog.errorf(PSTR(format),##__VA_ARGS__);
#define info(format,...) msgLog.infof(PSTR(format),##__VA_ARGS__);
#define debug(format,...) msgLog.debugf(PSTR(format),##__VA_ARGS__);

#endif //FIRMWARE_AURAMON_H
