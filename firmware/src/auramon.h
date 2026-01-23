//
// Created by Nicholas Wiersma on 2025/09/05.
//

#ifndef FIRMWARE_AURAMON_H
#define FIRMWARE_AURAMON_H

#include <Arduino.h>
#include <pico/mutex.h>
#include <SdFat.h>
#include <W5500lwIP.h>
#include <ModbusRTUMaster.h>
#include <Wire.h>
#include <PCF85063A.h>
#include <WebServer.h>
#include <ArduinoJSON.h>

#include "logger.h"
#include "datalog.h"
#include "task.h"
#include "modbus.h"
#include "api.h"
#include "device.h"

#define WAIT_FOR_SERIAL 1

#define MS_PER_HOUR 3600000UL

#define MESSAGE_LOG_PATH "aura-mon/log.txt"
#define DATA_LOG_PATH    "aura-mon/data.log"

#define LED_RED 10
#define LED_GREEN 11

#define ETH_INT  20
#define ETH_FREQ 40000000 // 40MHz

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

extern PCF85063A rtc;
extern bool      rtcRunning;

extern mutex_t sdMu;
extern SdFs    sd;

extern ModbusRTUMaster modbus;

extern WebServer server;

#define MAX_DEVICES 15
extern mutex_t deviceInfoMu;
extern inputDeviceInfo *deviceInfos[MAX_DEVICES];
extern inputDevice *devices[MAX_DEVICES];

extern dataLog datalog;

extern logger msgLog;
#define LOGE(format,...) msgLog.errorf(PSTR(format),##__VA_ARGS__);
#define LOGI(format,...) msgLog.infof(PSTR(format),##__VA_ARGS__);
#define LOGD(format,...) msgLog.debugf(PSTR(format),##__VA_ARGS__);

uint32_t timeSync(void *param);
uint32_t checkEthernet(void *param);
uint32_t logData(void *param);

void collect();

#endif //FIRMWARE_AURAMON_H
