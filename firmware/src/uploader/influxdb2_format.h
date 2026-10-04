//
// Created by Nicholas Wiersma on 2026/10/04.
//

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Pure, hardware-independent helpers for the InfluxDB2 uploader: settings
// parsing/validation and line-protocol formatting. Kept free of any
// HTTP/xbuf/data-log dependencies so they can be unit tested natively; the
// hardware-facing glue in influxdb2_uploader.cpp calls into these.

struct InfluxDB2Settings {
    String url;
    String org;
    String bucket;
    String token;
    String measurement;
};

// Parses settings out of the stored JSON object, defaulting "measurement" to
// "aura-mon" and trimming a trailing slash off "url". Returns false if a
// required field (url, org, bucket, token) is missing.
bool parseInfluxDB2Settings(JsonObjectConst settings, InfluxDB2Settings &out);

// Builds the /api/v2/write endpoint URL for the given settings.
String influxDB2Endpoint(const InfluxDB2Settings &settings);

// Escapes the characters line protocol requires escaping in a tag value
// (space, comma, equals) so device names containing them stay intact.
String influxDB2EscapeTagValue(const String &value);

// Computes a device's interval metrics (voltage, power, apparent power,
// energy, current, power factor) from a pair of log record hour-accumulators
// and appends a line-protocol point to body. Returns false (nothing
// appended) if the computed voltage/power/apparent power are not finite,
// e.g. because elapsedHours is zero.
bool appendInfluxDB2DevicePoint(String &body, const String &measurement, const String &deviceName,
                                 double fromVoltHrs, double toVoltHrs, double fromWattHrs, double toWattHrs,
                                 double fromVaHrs, double toVaHrs, double elapsedHours, uint32_t ts);

// Computes the mains frequency over the interval and appends a line-protocol
// point to body. Returns false (nothing appended) if the computed frequency
// is not finite or not positive.
bool appendInfluxDB2FrequencyPoint(String &body, const String &measurement, double fromHzHrs, double toHzHrs,
                                    double elapsedHours, uint32_t ts);
