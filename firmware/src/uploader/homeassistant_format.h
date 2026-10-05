//
// Created by Nicholas Wiersma on 2026/10/04.
//

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Pure, hardware-independent helpers for the Home Assistant uploader: settings
// parsing/validation and payload building. Kept free of any HTTP/xbuf/data-log
// dependencies so they can be unit tested natively; the hardware-facing glue
// in homeassistant_uploader.cpp calls into these and serializes the result
// straight into the request body.
//
// Payload shape (one POST per interval):
//   {"ts": 1733350230, "hz": 50.01,
//    "devices": [{"name": "Kitchen", "volts": 230.4, "amps": 0.52,
//                 "watts": 101.2, "wh": 0.84, "pf": 0.91}]}
// "wh" is the energy consumed during this interval (not a running total), so
// the receiving component can accumulate it itself, same as the existing
// pull-based integration's /energy handling.

struct HomeAssistantSettings {
    String url;       // Base URL of the Home Assistant instance, e.g. "http://homeassistant.local:8123"
    String webhookId; // Webhook ID registered by the Home Assistant component.
};

// Parses settings out of the stored JSON object, trimming a trailing slash
// off "url". Returns false if a required field (url, webhook_id) is missing.
bool parseHomeAssistantSettings(JsonObjectConst settings, HomeAssistantSettings &out);

// Builds the webhook URL to post payloads to.
String homeAssistantEndpoint(const HomeAssistantSettings &settings);

// Sets the payload's top-level "ts" field and, if the computed mains
// frequency is finite and positive, its "hz" field (omitted otherwise, e.g.
// because elapsedHours is zero).
void setHomeAssistantEnvelope(JsonDocument &doc, uint32_t ts, double fromHzHrs, double toHzHrs,
                               double elapsedHours);

// Computes a device's interval metrics (voltage, current, power, energy,
// power factor) from a pair of log record hour-accumulators and appends
// {"name", "volts", "amps", "watts", "wh", "pf"} to devices. Returns false
// (nothing appended) if the computed voltage/power/apparent power are not
// finite, e.g. because elapsedHours is zero.
bool appendHomeAssistantDevice(JsonArray devices, const String &name, double fromVoltHrs, double toVoltHrs,
                                double fromWattHrs, double toWattHrs, double fromVaHrs, double toVaHrs,
                                double elapsedHours);
