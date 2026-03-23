//
// Created by Nicholas Wiersma on 2025/10/10.
//

#pragma once

#include <Arduino.h>

enum LVL {
    LOG_LEVEL_UNKNOWN,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_ERROR
};

class Logger {
public:
    Logger();

    void printf(LVL lvl, const char *format, ...);

protected:
    bool   _restart;

    void write(LVL lvl, const char *buffer, size_t size);
};

inline Logger logger;

#define LOG(level, format, ...) logger.printf(level, format, ##__VA_ARGS__)
#define LOGE(format,...) LOG(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define LOGI(format,...) LOG(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define LOGD(format,...) LOG(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
