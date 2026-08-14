//
// Created by Nicholas Wiersma on 2025/11/04.
//

#ifndef UNIT_TEST
#include "auramon.h"
#include <lwip/inet.h>
#else
#include "../test/stubs/TestCore.h"
#include "storage.h"
#include "config.h"
#endif

constexpr uint32_t configFormat = 1;

// Copies a potentially-unaligned const char* (e.g. from an ArduinoJSON pool) into
// a 4-byte-aligned stack buffer byte-by-byte, then assigns that buffer to dest.
// This avoids the rp2350-memcpy.S crash that occurs when memcpy is given an
// unaligned source pointer. Strings longer than 255 characters are truncated.
static void assignFromJson(String &dest, const char *src) {
    if (!src) {
        dest = "";
        return;
    }
    alignas(4) char buf[256];
    size_t i = 0;
    while (i < sizeof(buf) - 1 && src[i] != '\0') {
        buf[i] = src[i];
        i++;
    }
    buf[i] = '\0';
    dest = buf;
}

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
    if (!deviceInfos[idx]) {
        deviceInfos[idx] = new InputDeviceInfo(address);
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
        InputDeviceInfo *info = ensureDeviceInfo(addr);
        if (!info) {
            continue;
        }
        info->enabled = entry["enabled"].is<bool>() ? entry["enabled"].as<bool>() : false;
        info->addr = addr;
        info->calibration = entry["calibration"].is<float>() ? entry["calibration"].as<float>() : 1.0f;
        info->reversed = entry["reversed"].is<bool>() ? entry["reversed"].as<bool>() : false;
        assignFromJson(info->name, entry["name"].is<const char *>() ? entry["name"].as<const char *>() : "");
    }

    mutex_exit(&deviceInfoMu);
}

void populateDevicesJson(JsonArray devicesArray) {
    mutex_enter_blocking(&deviceInfoMu);

    for (int i = 0; i < MAX_DEVICES; i++) {
        InputDeviceInfo *info = deviceInfos[i];
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

    mutex_exit(&deviceInfoMu);
}

error loadConfig() {
    JsonDocument doc;

    auto err = storage::run([&](storage::sdAccess &sd) -> error {
        if (auto e = sd.readJSON(CONFIG_LOG_PATH, doc); !e) {
            return {};
        }
        // Fall back to the temp file, which a save interrupted between writing
        // and renaming will have left behind.
        if (auto e = sd.readJSON(CONFIG_LOG_TMP_PATH, doc); e) {
            return newError("could not decode config file");
        }
        return {};
    });
    if (err) {
        return err;
    }

    return loadConfigJSON(doc);
}

error saveConfig() {
    JsonDocument doc;
    saveConfigJSON(doc);

    return storage::run([&](storage::sdAccess &sd) {
        return sd.writeJSON(CONFIG_LOG_PATH, CONFIG_LOG_TMP_PATH, doc);
    });
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
