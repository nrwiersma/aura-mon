#include "../lib/Error/src/errors.h"
#ifndef UNIT_TEST
#include "auramon.h"
#include <lwip/inet.h>
#else
#include "config.h"
#endif

constexpr uint32_t configFormat = 1;

error *loadNetworkConfigFromJson(JsonVariantConst netObj) {
    if (netObj.isNull()) {
        return nullptr;
    }

    if (netObj["hostname"].is<const char *>()) {
        netCfg.hostname = netObj["hostname"].as<const char *>();
    }
    if (netObj["ip"].is<const char *>()) {
        auto ip = netObj["ip"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return newError("invalid ip address");
        }
        netCfg.ip = ip;
    }
    if (netObj["gateway"].is<const char *>()) {
        auto ip = netObj["gateway"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return newError("invalid gateway address");
        }
        netCfg.gateway = ip;
    }
    if (netObj["mask"].is<const char *>()) {
        auto ip = netObj["mask"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return newError("invalid ip mask");
        }
        netCfg.mask = ip;
    }
    if (netObj["dns"].is<const char *>()) {
        auto ip = netObj["dns"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return newError("invalid dns address");
        }
        netCfg.dns = ip;
    }
    return nullptr;
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
}

inputDeviceInfo *ensureDeviceInfo(uint8_t address) {
    if (address == 0 || address > MAX_DEVICES) {
        return nullptr;
    }
    const size_t idx = address - 1;
    if (!deviceInfos[idx]) {
        deviceInfos[idx] = new inputDeviceInfo(address);
    }
    return deviceInfos[idx];
}

void removeDevicesFromLocked(size_t startIdx) {
    if (startIdx >= MAX_DEVICES) {
        return;
    }

    for (size_t i = startIdx; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            delete deviceInfos[i];
            deviceInfos[i] = nullptr;
        }
    }
}

void applyDevicesFromJson(JsonArrayConst devicesArr) {
    mutex_enter_blocking(&deviceInfoMu);
    removeDevicesFromLocked(devicesArr.size());

    for (JsonVariantConst entry: devicesArr) {
        if (!entry.is<JsonObjectConst>()) {
            continue;
        }
        uint8_t          addr = entry["address"].is<int>() ? entry["address"].as<uint8_t>() : 0;
        inputDeviceInfo *info = ensureDeviceInfo(addr);
        if (!info) {
            continue;
        }
        info->enabled = entry["enabled"].is<bool>() ? entry["enabled"].as<bool>() : false;
        info->addr = addr;
        info->calibration = entry["calibration"].is<float>() ? entry["calibration"].as<float>() : 1.0f;
        info->reversed = entry["reversed"].is<bool>() ? entry["reversed"].as<bool>() : false;
        info->name = entry["name"].is<const char *>() ? entry["name"].as<const char *>() : nullptr;
    }

    mutex_exit(&deviceInfoMu);
}

void populateDevicesJson(JsonArray devicesArray) {
    mutex_enter_blocking(&deviceInfoMu);

    for (int i = 0; i < MAX_DEVICES; i++) {
        inputDeviceInfo *info = deviceInfos[i];
        if (!info || !info->enabled) {
            continue;
        }
        JsonObject device = devicesArray.add<JsonObject>();
        device["enabled"] = info->enabled;
        device["address"] = info->addr;
        device["name"] = info->name;
        device["calibration"] = info->calibration;
        device["reversed"] = info->reversed;
    }

    mutex_exit(&deviceInfoMu);
}

error *loadConfig() {
    mutex_enter_blocking(&sdMu);
    FsFile file = sd.open(CONFIG_LOG_PATH, O_RDONLY);
    if (!file) {
        mutex_exit(&sdMu);
        return newError("could not open config file");
    }

    JsonDocument doc;

    auto err = deserializeJson(doc, file);

    file.close();
    mutex_exit(&sdMu);

    if (err) {
        return newError("could not decode config file");
    }

    return loadConfigJSON(doc);
}

void ensureConfigDirectoryLocked() {
    const char *path = CONFIG_LOG_PATH;
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return;
    }
    char   dir[64];
    size_t len = static_cast<size_t>(slash - path);
    if (len >= sizeof(dir)) {
        len = sizeof(dir) - 1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    sd.mkdir(dir);
}

error *saveConfig() {
    JsonDocument doc;
    saveConfigJSON(doc);

    mutex_enter_blocking(&sdMu);
    ensureConfigDirectoryLocked();
    FsFile file = sd.open(CONFIG_LOG_PATH, O_RDWR | O_CREAT | O_TRUNC);
    if (!file) {
        mutex_exit(&sdMu);
        return newError("could not create config file");
    }
    file.truncate();
    if (serializeJson(doc, file) == 0) {
        mutex_exit(&sdMu);
        return newError("could not write config file");
    }
    file.flush();
    file.close();
    mutex_exit(&sdMu);
    return nullptr;
}

error *loadConfigJSON(const JsonDocument &doc) {
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

    return nullptr;
}

void saveConfigJSON(JsonDocument &doc) {
    doc.clear();
    doc["format"] = configFormat;

    auto network = doc["network"].to<JsonObject>();
    writeNetworkConfigToJson(network);

    auto devicesArray = doc["devices"].to<JsonArray>();
    populateDevicesJson(devicesArray);
}
