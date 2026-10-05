//
// Created by Nicholas Wiersma on 2026/10/04.
//

#include <cmath>

#include "json_util.h"
#include "uploader/homeassistant_format.h"

bool parseHomeAssistantSettings(JsonObjectConst settings, HomeAssistantSettings &out) {
    assignFromJson(out.url, settings["url"].is<const char *>() ? settings["url"].as<const char *>() : "");
    assignFromJson(out.webhookId,
                   settings["webhook_id"].is<const char *>() ? settings["webhook_id"].as<const char *>() : "");

    if (out.url.endsWith("/")) {
        out.url.remove(out.url.length() - 1);
    }

    return !out.url.isEmpty() && !out.webhookId.isEmpty();
}

String homeAssistantEndpoint(const HomeAssistantSettings &settings) {
    String url = settings.url;
    url += "/api/webhook/";
    url += settings.webhookId;
    return url;
}

void setHomeAssistantEnvelope(JsonDocument &doc, uint32_t ts, double fromHzHrs, double toHzHrs,
                               double elapsedHours) {
    doc["ts"] = ts;

    const double hz = (toHzHrs - fromHzHrs) / elapsedHours;
    if (std::isfinite(hz) && hz > 0) {
        doc["hz"] = hz;
    }
}

bool appendHomeAssistantDevice(JsonArray devices, const String &name, double fromVoltHrs, double toVoltHrs,
                                double fromWattHrs, double toWattHrs, double fromVaHrs, double toVaHrs,
                                double elapsedHours) {
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

    JsonObject device = devices.add<JsonObject>();
    device["name"]  = name.c_str();
    device["volts"] = voltage;
    device["amps"]  = current;
    device["watts"] = power;
    device["wh"]    = energyWh;
    device["pf"]    = powerFactor;
    return true;
}
