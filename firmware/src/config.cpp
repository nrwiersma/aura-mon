//
// Created by Nicholas Wiersma on 2025/11/04.
//

#ifndef UNIT_TEST
#include "auramon.h"
#include <lwip/inet.h>
#else
#include "../test/stubs/TestCore.h"
#include "config.h"
#endif

#include "json_util.h"

constexpr uint32_t configFormat = 1;

error loadNetworkConfigFromJson(JsonVariantConst netObj) {
    if (netObj.isNull()) {
        return {};
    }

    if (netObj["hostname"].is<const char *>()) {
        assignFromJson(netCfg.hostname, netObj["hostname"].as<const char *>());
    }
    if (netObj["ip"].is<const char *>()) {
        auto ip = netObj["ip"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return newError("invalid ip address");
            }
            assignFromJson(netCfg.ip, ip);
        }
    }
    if (netObj["gateway"].is<const char *>()) {
        auto ip = netObj["gateway"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return newError("invalid gateway address");
            }
            assignFromJson(netCfg.gateway, ip);
        }
    }
    if (netObj["mask"].is<const char *>()) {
        auto ip = netObj["mask"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return newError("invalid ip mask");
            }
            assignFromJson(netCfg.mask, ip);
        }
    }
    if (netObj["dns"].is<const char *>()) {
        auto ip = netObj["dns"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return newError("invalid dns address");
            }
            assignFromJson(netCfg.dns, ip);
        }
    }
    if (netObj["mac"].is<const char *>()) {
        auto mac = netObj["mac"].as<const char *>();
        uint8_t macBytes[6];
        if (strlen(mac) > 0 && !parseMac(mac, macBytes)) {
            return newError("invalid mac address");
        }
        assignFromJson(netCfg.mac, mac);
    }
    return {};
}

void writeNetworkConfigToJson(JsonObject obj) {
    if (!obj) {
        return;
    }
    obj["hostname"] = netCfg.hostname.c_str();
    obj["ip"] = netCfg.ip.c_str();
    obj["gateway"] = netCfg.gateway.c_str();
    obj["mask"] = netCfg.mask.c_str();
    obj["dns"] = netCfg.dns.c_str();
    obj["mac"] = netCfg.mac.c_str();
}

InputDevice *ensureDevice(uint8_t address) {
    if (address == 0 || address > MAX_DEVICES) {
        return nullptr;
    }
    const size_t idx = address - 1;
    if (!devices[idx]) {
        devices[idx] = new InputDevice(address);
    }
    return devices[idx];
}

static void removeDevicesNotIn(JsonArrayConst devicesArr) {
    bool keep[MAX_DEVICES] = {};
    for (JsonVariantConst entry: devicesArr) {
        int addr = entry["address"].is<int>() ? entry["address"].as<int>() : 0;
        if (addr >= 1 && addr <= MAX_DEVICES) {
            keep[addr - 1] = true;
        }
    }

    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (!keep[i] && devices[i]) {
            delete devices[i];
            devices[i] = nullptr;
        }
    }
}

void applyDevicesFromJson(JsonArrayConst devicesArr) {
    removeDevicesNotIn(devicesArr);

    for (JsonVariantConst entry: devicesArr) {
        if (!entry.is<JsonObjectConst>()) {
            continue;
        }
        uint8_t      addr = entry["address"].is<int>() ? entry["address"].as<uint8_t>() : 0;
        InputDevice *device = ensureDevice(addr);
        if (!device) {
            continue;
        }
        device->enabled = entry["enabled"].is<bool>() ? entry["enabled"].as<bool>() : false;
        device->addr = addr;
        device->calibration = entry["calibration"].is<float>() ? entry["calibration"].as<float>() : 1.0f;
        device->reversed = entry["reversed"].is<bool>() ? entry["reversed"].as<bool>() : false;
        assignFromJson(device->name, entry["name"].is<const char *>() ? entry["name"].as<const char *>() : "");
    }
}

void populateDevicesJson(JsonArray devicesArray) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        InputDevice *device = devices[i];
        if (!device) {
            continue;
        }
        JsonObject dev = devicesArray.add<JsonObject>();
        dev["enabled"] = device->enabled;
        dev["address"] = device->addr;
        dev["name"] = device->name.c_str();
        dev["calibration"] = device->calibration;
        dev["reversed"] = device->reversed;
    }
}

UploaderConfig *ensureUploaderConfig(const char *id) {
    for (int i = 0; i < MAX_UPLOADERS; i++) {
        if (uploaderConfigs[i] && uploaderConfigs[i]->id == id) {
            return uploaderConfigs[i];
        }
    }
    for (int i = 0; i < MAX_UPLOADERS; i++) {
        if (!uploaderConfigs[i]) {
            uploaderConfigs[i] = new UploaderConfig();
            assignFromJson(uploaderConfigs[i]->id, id);
            return uploaderConfigs[i];
        }
    }
    return nullptr;
}

void applyUploadersFromJson(JsonArrayConst uploadersArr) {
    bool used[MAX_UPLOADERS] = {};

    for (JsonVariantConst entry: uploadersArr) {
        if (!entry.is<JsonObjectConst>()) {
            continue;
        }
        const char *id = entry["id"].is<const char *>() ? entry["id"].as<const char *>() : "";
        if (strlen(id) == 0) {
            continue;
        }
        UploaderConfig *cfg = ensureUploaderConfig(id);
        if (!cfg) {
            LOGE("Too many uploaders configured, ignoring '%s'", id);
            continue;
        }

        cfg->enabled = entry["enabled"].is<bool>() ? entry["enabled"].as<bool>() : false;
        assignFromJson(cfg->type, entry["type"].is<const char *>() ? entry["type"].as<const char *>() : "");
        cfg->interval = entry["interval"].is<int>() ? entry["interval"].as<uint32_t>() : 60;

        cfg->settings = "";
        if (entry["settings"].is<JsonObjectConst>()) {
            char   buf[512];
            size_t n = serializeJson(entry["settings"], buf, sizeof(buf));
            if (n > 0) {
                cfg->settings = buf;
            }
        }

        for (int i = 0; i < MAX_UPLOADERS; i++) {
            if (uploaderConfigs[i] == cfg) {
                used[i] = true;
                break;
            }
        }
    }

    // Anything not present in this config was removed.
    for (int i = 0; i < MAX_UPLOADERS; i++) {
        if (uploaderConfigs[i] && !used[i]) {
            delete uploaderConfigs[i];
            uploaderConfigs[i] = nullptr;
        }
    }

    uploadersChanged = true;
}

void populateUploadersJson(JsonArray uploadersArray) {
    for (int i = 0; i < MAX_UPLOADERS; i++) {
        UploaderConfig *cfg = uploaderConfigs[i];
        if (!cfg) {
            continue;
        }
        JsonObject entry = uploadersArray.add<JsonObject>();
        entry["id"] = cfg->id.c_str();
        entry["type"] = cfg->type.c_str();
        entry["enabled"] = cfg->enabled;
        entry["interval"] = cfg->interval;

        if (cfg->settings.length() > 0) {
            JsonDocument settingsDoc;
            if (!deserializeJson(settingsDoc, cfg->settings.c_str())) {
                entry["settings"].set(settingsDoc.as<JsonObjectConst>());
            }
        }
    }
}

error loadConfig() {
    FsFile file = sd.open(CONFIG_LOG_PATH, O_RDONLY);
    if (!file) {
        // Fall back to the last successful temp file.
        file = sd.open(CONFIG_LOG_TMP_PATH, O_RDONLY);
        if (!file) {
            return newError("could not open config file");
        }
    }

    JsonDocument doc;

    auto err = deserializeJson(doc, file);

    file.close();

    if (err) {
        return newError("could not decode config file");
    }

    return loadConfigJSON(doc);
}

void ensureConfigDirectory() {
    const char *path = CONFIG_LOG_PATH;
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return;
    }
    char dir[64];
    auto len = static_cast<size_t>(slash - path);
    if (len >= sizeof(dir)) {
        len = sizeof(dir) - 1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    sd.mkdir(dir);
}

error saveConfig() {
    JsonDocument doc;
    saveConfigJSON(doc);

    ensureConfigDirectory();
    FsFile file = sd.open(CONFIG_LOG_TMP_PATH, O_RDWR | O_CREAT | O_TRUNC);
    if (!file) {
        return newError("could not create config file");
    }
    if (serializeJson(doc, file) == 0) {
        file.close();
        return newError("could not write config file");
    }
    file.flush();
    file.close();
    sd.remove(CONFIG_LOG_PATH);
    if (!sd.rename(CONFIG_LOG_TMP_PATH, CONFIG_LOG_PATH)) {
        return newError("could not rename config file");
    }
    return {};
}

error loadConfigJSON(const JsonDocument &doc) {
    JsonVariantConst root = doc.as<JsonVariantConst>();
    if (root.isNull()) {
        return newError("config object is empty");
    }

    if (root["format"].is<uint32_t>() && root["format"].as<uint32_t>() != configFormat) {
        return newError("config format mismatch");
    }

    if (auto err = loadNetworkConfigFromJson(root["network"]); err) {
        return err;
    }

    if (root["devices"].is<JsonArrayConst>()) {
        applyDevicesFromJson(root["devices"].as<JsonArrayConst>());
    }

    if (root["uploaders"].is<JsonArrayConst>()) {
        applyUploadersFromJson(root["uploaders"].as<JsonArrayConst>());
    }

    return {};
}

void saveConfigJSON(JsonDocument &doc) {
    doc.clear();
    doc["format"] = configFormat;

    auto network = doc["network"].to<JsonObject>();
    writeNetworkConfigToJson(network);

    auto devicesArray = doc["devices"].to<JsonArray>();
    populateDevicesJson(devicesArray);

    auto uploadersArray = doc["uploaders"].to<JsonArray>();
    populateUploadersJson(uploadersArray);
}
