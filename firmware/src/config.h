#pragma once

#include <ArduinoJson.h>
#include <expected>

std::expected<void, String> loadConfig();
std::expected<void, String> saveConfig();
std::expected<void, String> loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
