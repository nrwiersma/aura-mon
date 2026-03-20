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

constexpr uint32_t configFormat = 1;

std::expected<void, String> loadNetworkConfigFromJson(JsonVariantConst netObj) {
    if (netObj.isNull()) {
        return {};
    }

    if (netObj["hostname"].is<const char *>()) {
        netCfg.hostname = netObj["hostname"].as<const char *>();
    }
    if (netObj["ip"].is<const char *>()) {
        auto ip = netObj["ip"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return std::unexpected<String>("invalid ip address");
            }
            netCfg.ip = ip;
        }
    }
    if (netObj["gateway"].is<const char *>()) {
        auto ip = netObj["gateway"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return std::unexpected<String>("invalid gateway address");
            }
            netCfg.gateway = ip;
        }
    }
    if (netObj["mask"].is<const char *>()) {
        auto ip = netObj["mask"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return std::unexpected<String>("invalid ip mask");
            }
            netCfg.mask = ip;
        }
    }
    if (netObj["dns"].is<const char *>()) {
        auto ip = netObj["dns"].as<const char *>();
        if (strlen(ip) > 0) {
            if (ipaddr_addr(ip) == IPADDR_NONE) {
                return std::unexpected<String>("invalid dns address");
            }
            netCfg.dns = ip;
        }
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
}

InputDeviceInfo *ensureDeviceInfo(uint8_t address) {
    if (address == 0 || address > MAX_DEVICES) {
        return nullptr;
    }
    const size_t idx = address - 1;
    if (!registry.infos[idx]) {
        registry.infos[idx] = new InputDeviceInfo(address);
    }
    return registry.infos[idx];
}

void removeDevicesFromLocked(size_t startIdx) {
    if (startIdx >= MAX_DEVICES) {
        return;
    }

    for (size_t i = startIdx; i < MAX_DEVICES; i++) {
        if (registry.infos[i]) {
            delete registry.infos[i];
            registry.infos[i] = nullptr;
        }
    }
}

void applyDevicesFromJson(JsonArrayConst devicesArr) {
    mutex_enter_blocking(&registry.infoMu);
    removeDevicesFromLocked(devicesArr.size());

    for (JsonVariantConst entry: devicesArr) {
        if (!entry.is<JsonObjectConst>()) {
            continue;
        }
        uint8_t          addr = entry["address"].is<int>() ? entry["address"].as<uint8_t>() : 0;
        InputDeviceInfo *info = ensureDeviceInfo(addr);
        if (!info) {
            continue;
        }
        info->enabled = entry["enabled"].is<bool>() ? entry["enabled"].as<bool>() : false;
        info->addr = addr;
        info->calibration = entry["calibration"].is<float>() ? entry["calibration"].as<float>() : 1.0f;
        info->reversed = entry["reversed"].is<bool>() ? entry["reversed"].as<bool>() : false;
        info->name = entry["name"].is<const char *>() ? entry["name"].as<const char *>() : "";
    }

    mutex_exit(&registry.infoMu);
}

void populateDevicesJson(JsonArray devicesArray) {
    mutex_enter_blocking(&registry.infoMu);

    for (int i = 0; i < MAX_DEVICES; i++) {
        InputDeviceInfo *info = registry.infos[i];
        if (!info || !info->enabled) {
            continue;
        }
        JsonObject device = devicesArray.add<JsonObject>();
        device["enabled"] = info->enabled;
        device["address"] = info->addr;
        device["name"] = info->name.c_str();
        device["calibration"] = info->calibration;
        device["reversed"] = info->reversed;
    }

    mutex_exit(&registry.infoMu);
}

std::expected<void, String> loadConfig() {
    SafeSdFile file = sd.open(CONFIG_LOG_PATH, O_RDONLY);
    if (!file) {
        file = sd.open(CONFIG_LOG_TMP_PATH, O_RDONLY);
        if (!file) {
            return std::unexpected<String>("could not open config file");
        }
    }

    const uint32_t size = file.size();
    auto          *buf  = new char[size + 1];
    file.read(buf, size);
    buf[size] = '\0';
    file.close();

    JsonDocument doc;
    auto err = deserializeJson(doc, buf, size);
    delete[] buf;

    if (err) {
        return std::unexpected<String>("could not decode config file");
    }

    return loadConfigJSON(doc);
}

std::expected<void, String> saveConfig() {
    JsonDocument doc;
    saveConfigJSON(doc);

    String json;
    serializeJson(doc, json);

    return sd.with([&](auto &fs) -> std::expected<void, String> {
        const char *path = CONFIG_LOG_PATH;
        const char *slash = strrchr(path, '/');
        if (slash) {
            char dir[64];
            auto len = static_cast<size_t>(slash - path);
            if (len >= sizeof(dir)) len = sizeof(dir) - 1;
            memcpy(dir, path, len);
            dir[len] = '\0';
            fs.mkdir(dir);
        }

        SafeSdFile file = fs.open(CONFIG_LOG_TMP_PATH, O_RDWR | O_CREAT | O_TRUNC);
        if (!file)
            return std::unexpected<String>("could not create config file");
        if (file.write(json.c_str(), json.length()) == 0) {
            file.close();
            return std::unexpected<String>("could not write config file");
        }
        file.flush();
        file.close();

        fs.remove(CONFIG_LOG_PATH);
        if (!fs.rename(CONFIG_LOG_TMP_PATH, CONFIG_LOG_PATH))
            return std::unexpected<String>("could not rename config file");

        return {};
    });
}

std::expected<void, String> loadConfigJSON(const JsonDocument &doc) {
    JsonVariantConst root = doc.as<JsonVariantConst>();
    if (root.isNull()) {
        return std::unexpected<String>("config object is empty");
    }

    if (root["format"].is<uint32_t>() && root["format"].as<uint32_t>() != configFormat) {
        return std::unexpected<String>("config format mismatch");
    }

    if (auto res = loadNetworkConfigFromJson(root["network"]); !res) {
        return res;
    }

    if (root["devices"].is<JsonArrayConst>()) {
        applyDevicesFromJson(root["devices"].as<JsonArrayConst>());
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
}
