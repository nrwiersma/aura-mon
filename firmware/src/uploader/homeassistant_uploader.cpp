//
// Created by Nicholas Wiersma on 2026/10/04.
//

#include "auramon.h"
#include "uploader/homeassistant_uploader.h"

bool HomeAssistantUploader::applySettings(JsonObjectConst settings) {
    return parseHomeAssistantSettings(settings, _settings);
}

String HomeAssistantUploader::endpoint() const {
    return homeAssistantEndpoint(_settings);
}

bool HomeAssistantUploader::buildRequest(uint32_t fromTS, uint32_t toTS, xbuf &body, String &contentType) {
    LogRecord fromRec, toRec;
    if (datalog.read(fromTS, &fromRec) || datalog.read(toTS, &toRec)) {
        return false;
    }

    const double elapsedHours = toRec.logHours - fromRec.logHours;
    if (elapsedHours <= 0) {
        return false;
    }

    JsonDocument doc;
    setHomeAssistantEnvelope(doc, toTS, fromRec.hzHrs, toRec.hzHrs, elapsedHours);
    JsonArray devices = doc["devices"].to<JsonArray>();

    bool wrote = false;

    if (!mutex_enter_timeout_ms(&deviceInfoMu, 100)) {
        LOGE("homeassistant %s: could not acquire deviceInfoMu", id().c_str());
        return false;
    }

    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        InputDeviceInfo *info = deviceInfos[i];
        if (!info || !info->isEnabled() || info->name.isEmpty()) {
            continue;
        }

        wrote |= appendHomeAssistantDevice(devices, info->name, fromRec.voltHrs[i], toRec.voltHrs[i],
                                            fromRec.wattHrs[i], toRec.wattHrs[i], fromRec.vaHrs[i], toRec.vaHrs[i],
                                            elapsedHours);
    }

    mutex_exit(&deviceInfoMu);

    if (!wrote) {
        return false;
    }

    serializeJson(doc, body);
    contentType = "application/json";
    return true;
}
