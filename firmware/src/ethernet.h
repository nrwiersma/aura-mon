//
// Created by Nicholas Wiersma on 2026/03/19.
//

#pragma once

// Parses "de:ad:be:ef:fe:ee" into out. Returns false on any malformed input.
inline bool parseMac(const char *s, uint8_t out[6]) {
    if (!s) {
        return false;
    }
    unsigned int b[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (b[i] > 0xFF) {
            return false;
        }
        out[i] = static_cast<uint8_t>(b[i]);
    }
    return true;
}

struct NetworkConfig {
    String hostname;
    String ip;
    String gateway;
    String mask;
    String dns;
    // Empty means use the compiled-in default MAC.
    String mac;

    // Resolves the effective MAC: the configured one when set and valid,
    // otherwise the provided default.
    void macAddress(uint8_t out[6], const uint8_t defp[6]) const {
        if (!mac.isEmpty() && parseMac(mac.c_str(), out)) {
            return;
        }
        memcpy(out, defp, 6);
    }

    NetworkConfig()
        : hostname("aura-mon"),
          ip(""),
          gateway(""),
          mask("255.255.255.0"),
          dns("8.8.8.8"),
          mac("") {
    }

    bool hasIP() { return (!ip.isEmpty()); }
};
