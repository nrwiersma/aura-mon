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

// UploaderConfig is the persisted, user-editable configuration for a single
// uploader instance (e.g. one InfluxDB2 target).
//
// "settings" carries the type-specific fields (url, token, bucket, ...) as a
// serialized JSON object. The concrete Uploader for "type" parses it itself.
class UploaderConfig {
public:
    bool     enabled;
    String   id;       // Unique, user assigned name (e.g. "influx-main").
    String   type;      // Selects the uploader implementation (e.g. "influxdb2").
    uint32_t interval;  // Seconds between upload attempts.
    String   settings;  // Type-specific settings, serialized as JSON.

    UploaderConfig() : enabled(false), interval(60) {
    }
};
