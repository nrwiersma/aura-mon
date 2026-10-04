//
// Created by Nicholas Wiersma on 2026/10/04.
//

#include "auramon.h"
#include "uploader/influxdb2_uploader.h"

bool InfluxDB2Uploader::applySettings(JsonObjectConst settings) {
    return parseInfluxDB2Settings(settings, _settings);
}

String InfluxDB2Uploader::endpoint() const {
    return influxDB2Endpoint(_settings);
}

void InfluxDB2Uploader::setRequestHeaders(asyncHTTPrequest &request) {
    String auth = "Token ";
    auth += _settings.token;
    request.setReqHeader("Authorization", auth.c_str());
}

bool InfluxDB2Uploader::buildRequest(uint32_t fromTS, uint32_t toTS, xbuf &body, String &contentType) {
    LogRecord fromRec, toRec;
    if (datalog.read(fromTS, &fromRec) || datalog.read(toTS, &toRec)) {
        return false;
    }

    const double elapsedHours = toRec.logHours - fromRec.logHours;
    if (elapsedHours <= 0) {
        return false;
    }

    String text;
    bool   wrote = false;

    if (!mutex_enter_timeout_ms(&deviceInfoMu, 100)) {
        LOGE("influxdb2 %s: could not acquire deviceInfoMu", id().c_str());
        return false;
    }

    for (uint8_t i = 0; i < MAX_DEVICES; i++) {
        InputDeviceInfo *info = deviceInfos[i];
        if (!info || !info->isEnabled() || info->name.isEmpty()) {
            continue;
        }

        wrote |= appendInfluxDB2DevicePoint(text, _settings.measurement, info->name, fromRec.voltHrs[i],
                                             toRec.voltHrs[i], fromRec.wattHrs[i], toRec.wattHrs[i],
                                             fromRec.vaHrs[i], toRec.vaHrs[i], elapsedHours, toTS);
    }

    mutex_exit(&deviceInfoMu);

    wrote |= appendInfluxDB2FrequencyPoint(text, _settings.measurement, fromRec.hzHrs, toRec.hzHrs, elapsedHours,
                                           toTS);

    if (!wrote) {
        return false;
    }

    body.write(text.c_str());
    contentType = "text/plain; charset=utf-8";
    return true;
}
