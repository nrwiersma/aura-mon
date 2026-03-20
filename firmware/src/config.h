//
// Created by Nicholas Wiersma on 2026/03/19.
//

#pragma once

#include <ArduinoJson.h>
#include <expected>

std::expected<void, String> loadConfig();
std::expected<void, String> saveConfig();
std::expected<void, String> loadConfigJSON(const JsonDocument &doc);
void saveConfigJSON(JsonDocument &doc);
