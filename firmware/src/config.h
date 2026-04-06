//
// Created by Nicholas Wiersma on 2026/03/19.
//

#pragma once

#include <ArduinoJson.h>
#include <errors.h>

error loadConfig();
error saveConfig();
error loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
