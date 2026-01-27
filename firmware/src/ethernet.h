#pragma once

struct NetworkConfig {
    String hostname;
    String ip;
    String gateway;
    String mask;
    String dns;

    NetworkConfig()
        : hostname("aura-mon"),
          ip("0.0.0.0"),
          gateway("0.0.0.0"),
          mask("255.255.255.0"),
          dns("8.8.8.8") {
    }

    bool hasIP() { return (!ip.isEmpty()); }
};
