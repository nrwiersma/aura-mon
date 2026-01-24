#pragma once

#ifdef UNIT_TEST
#include "../stubs/TestAuraMon.h"
#endif
#include <ArduinoJson.h>

bool loadConfig();
bool saveConfig();
bool loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
