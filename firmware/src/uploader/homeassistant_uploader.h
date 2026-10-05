//
// Created by Nicholas Wiersma on 2026/10/04.
//

#pragma once

#include "uploader/homeassistant_format.h"
#include "uploader/uploader.h"

// HomeAssistantUploader posts one JSON payload per interval - mains frequency
// plus one entry per enabled, named device - to a Home Assistant webhook.
// See homeassistant_format.h for the payload shape.
//
// Settings (UploaderConfig::settings):
//   url         Base URL of the Home Assistant instance, e.g. "http://homeassistant.local:8123"
//   webhook_id  Webhook ID registered by the Home Assistant component.
class HomeAssistantUploader : public Uploader {
public:
    HomeAssistantUploader(const String &id) : Uploader(id, "homeassistant") {
    }

protected:
    bool   applySettings(JsonObjectConst settings) override;
    bool   buildRequest(uint32_t fromTS, uint32_t toTS, xbuf &body, String &contentType) override;
    String endpoint() const override;

private:
    HomeAssistantSettings _settings;
};
