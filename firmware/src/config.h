#pragma once

#include <ArduinoJson.h>
#include <errors.h>

Error* loadConfig();
Error* saveConfig();
Error* loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
