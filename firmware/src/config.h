#pragma once

#ifdef UNIT_TEST
#include "../stubs/TestAuraMon.h"
#endif
#include <ArduinoJson.h>
#include <errors.h>

error* loadConfig();
error* saveConfig();
error* loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
