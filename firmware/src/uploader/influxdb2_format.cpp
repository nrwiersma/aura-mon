//
// Created by Nicholas Wiersma on 2026/10/04.
//

#include <cmath>
#include <cstdio>

#include "json_util.h"
#include "uploader/influxdb2_format.h"

bool parseInfluxDB2Settings(JsonObjectConst settings, InfluxDB2Settings &out) {
    assignFromJson(out.url, settings["url"].is<const char *>() ? settings["url"].as<const char *>() : "");
    assignFromJson(out.org, settings["org"].is<const char *>() ? settings["org"].as<const char *>() : "");
    assignFromJson(out.bucket, settings["bucket"].is<const char *>() ? settings["bucket"].as<const char *>() : "");
    assignFromJson(out.token, settings["token"].is<const char *>() ? settings["token"].as<const char *>() : "");
    assignFromJson(out.measurement,
                   settings["measurement"].is<const char *>() ? settings["measurement"].as<const char *>() : "");

    if (out.url.endsWith("/")) {
        out.url.remove(out.url.length() - 1);
    }
    if (out.measurement.isEmpty()) {
        out.measurement = "aura-mon";
    }

    return !out.url.isEmpty() && !out.org.isEmpty() && !out.bucket.isEmpty() && !out.token.isEmpty();
}

String influxDB2Endpoint(const InfluxDB2Settings &settings) {
    String url = settings.url;
    url += "/api/v2/write?precision=s&org=";
    url += settings.org;
    url += "&bucket=";
    url += settings.bucket;
    return url;
}

String influxDB2EscapeTagValue(const String &value) {
    String      out;
    const char *s = value.c_str();
    out.reserve(value.length());
    for (size_t i = 0; s[i] != '\0'; i++) {
        const char c = s[i];
        if (c == ' ' || c == ',' || c == '=') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

bool appendInfluxDB2DevicePoint(String &body, const String &measurement, const String &deviceName,
                                 double fromVoltHrs, double toVoltHrs, double fromWattHrs, double toWattHrs,
                                 double fromVaHrs, double toVaHrs, double elapsedHours, uint32_t ts) {
    const double voltage = (toVoltHrs - fromVoltHrs) / elapsedHours;
    double       energyWh = toWattHrs - fromWattHrs;
    const double power = energyWh / elapsedHours;
    const double apparentPower = (toVaHrs - fromVaHrs) / elapsedHours;
    if (!std::isfinite(voltage) || !std::isfinite(power) || !std::isfinite(apparentPower)) {
        return false;
    }
    if (energyWh < 0) {
        energyWh = 0;
    }
    const double current = voltage != 0.0 ? apparentPower / voltage : 0.0;
    const double powerFactor = apparentPower > 0.0 ? power / apparentPower : 0.0;

    char line[160];
    snprintf(line, sizeof(line), "%s,device=%s volts=%.2f,amps=%.3f,watts=%.2f,va=%.2f,wh=%.6f,pf=%.4f %lu\n",
             measurement.c_str(), influxDB2EscapeTagValue(deviceName).c_str(), voltage, current, power,
             apparentPower, energyWh, powerFactor, static_cast<unsigned long>(ts));
    body += line;
    return true;
}

bool appendInfluxDB2FrequencyPoint(String &body, const String &measurement, double fromHzHrs, double toHzHrs,
                                    double elapsedHours, uint32_t ts) {
    const double hz = (toHzHrs - fromHzHrs) / elapsedHours;
    if (!std::isfinite(hz) || hz <= 0) {
        return false;
    }

    char line[96];
    snprintf(line, sizeof(line), "%s,device=mains hz=%.2f %lu\n", measurement.c_str(), hz,
             static_cast<unsigned long>(ts));
    body += line;
    return true;
}
