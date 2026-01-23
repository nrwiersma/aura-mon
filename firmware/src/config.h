#pragma once

#include <ArduinoJson.h>

bool loadConfig();
bool saveConfig();
bool loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
