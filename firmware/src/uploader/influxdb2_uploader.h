//
// Created by Nicholas Wiersma on 2026/10/04.
//

#pragma once

#include "uploader/influxdb2_format.h"
#include "uploader/uploader.h"

// InfluxDB2Uploader writes one line-protocol point per enabled, named device
// for each interval (plus one point for the mains frequency) to an InfluxDB
// 2.x bucket via its /api/v2/write endpoint.
//
// Settings (UploaderConfig::settings):
//   url          Base URL of the InfluxDB server, e.g. "http://192.168.1.10:8086"
//   org          Organization name
//   bucket       Bucket name
//   token        API token, sent as "Authorization: Token <token>"
//   measurement  Measurement name (default "aura-mon")
class InfluxDB2Uploader : public Uploader {
public:
    InfluxDB2Uploader(const String &id) : Uploader(id, "influxdb2") {
    }

protected:
    bool   applySettings(JsonObjectConst settings) override;
    bool   buildRequest(uint32_t fromTS, uint32_t toTS, xbuf &body, String &contentType) override;
    String endpoint() const override;
    void   setRequestHeaders(asyncHTTPrequest &request) override;

private:
    InfluxDB2Settings _settings;
};
