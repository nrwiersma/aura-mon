#include "auramon.h"
#include <lwip/inet.h>

constexpr uint32_t kConfigFormat = 2;

bool loadNetworkConfigFromJson(JsonVariantConst netObj) {
    if (netObj.isNull()) {
        return true;
    }

    if (netObj["hostname"].is<const char *>()) {
        netCfg.hostname = netObj["hostname"].as<const char *>();
    }
    if (netObj["ip"].is<const char *>()) {
        auto ip = netObj["ip"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return false;
        }
        netCfg.ip = ip;
    }
    if (netObj["gateway"].is<const char *>()) {
        auto ip = netObj["gateway"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return false;
        }
        netCfg.gateway = ip;
    }
    if (netObj["mask"].is<const char *>()) {
        auto ip = netObj["mask"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return false;
        }
        netCfg.mask = ip;
    }
    if (netObj["dns"].is<const char *>()) {
        auto ip = netObj["dns"].as<const char *>();
        if (ipaddr_addr(ip) == IPADDR_NONE) {
            return false;
        }
        netCfg.dns = ip;
    }
    return true;
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

void disableAllDevicesLocked() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (deviceInfos[i]) {
            deviceInfos[i]->enabled = false;
        }
    }
}

void applyDevicesFromJson(JsonArrayConst devicesArr) {
    mutex_enter_blocking(&deviceInfoMu);
    disableAllDevicesLocked();

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
        info->name = entry["name"].is<const char*>() ? entry["name"].as<const char*>() : nullptr;
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

bool loadConfig() {
    mutex_enter_blocking(&sdMu);
    FsFile file = sd.open(CONFIG_LOG_PATH, O_RDONLY);
    if (!file) {
        mutex_exit(&sdMu);
        return false;
    }

    JsonDocument doc;

    auto err = deserializeJson(doc, file);

    file.close();
    mutex_exit(&sdMu);

    if (err) {
        return false;
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

bool saveConfig() {
    JsonDocument doc;
    saveConfigJSON(doc);

    mutex_enter_blocking(&sdMu);
    ensureConfigDirectoryLocked();
    FsFile file = sd.open(CONFIG_LOG_PATH, O_RDWR | O_CREAT | O_TRUNC);
    if (!file) {
        mutex_exit(&sdMu);
        return false;
    }
    file.truncate();
    if (serializeJson(doc, file) == 0) {
        mutex_exit(&sdMu);
        return false;
    }
    file.flush();
    file.close();
    mutex_exit(&sdMu);
    return true;
}

bool loadConfigJSON(const JsonDocument &doc) {
    JsonVariantConst root = doc.as<JsonVariantConst>();
    if (root.isNull()) {
        return false;
    }

    if (root["format"].is<uint32_t>() && root["format"].as<uint32_t>() != kConfigFormat) {
        return false;
    }

    if (!loadNetworkConfigFromJson(root["network"])) {
        return false;
    }

    if (root["devices"].is<JsonArrayConst>()) {
        applyDevicesFromJson(root["devices"].as<JsonArrayConst>());
    } else {
        return false;
    }

    return true;
}

void saveConfigJSON(JsonDocument &doc) {
    doc.clear();
    doc["format"] = kConfigFormat;

    auto network = doc["network"].to<JsonObject>();
    writeNetworkConfigToJson(network);

    auto devicesArray = doc["devices"].to<JsonArray>();
    populateDevicesJson(devicesArray);
}
