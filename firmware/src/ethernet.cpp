//
// Created by Nicholas Wiersma on 2025/10/20.
//

#include "auramon.h"

uint32_t checkEthernet(void *param) {
    (void) param;

    static time_t lastDisconnect = time(NULL);
    static time_t lastConnect = 0;

    if (eth.isLinked() && eth.connected()) {
        if(!lastConnect) {
            lastConnect = time(NULL);
            LOGI("Ethernet connected: IP=%s", eth.localIP().toString().c_str());
        }
    } else if (!eth.isLinked()) {
        if (lastConnect) {
            lastConnect = 0;
            lastDisconnect = time(NULL);
            LOGI("Ethernet disconnected");
        } else if (time(NULL) - lastDisconnect > 60*60) {
            LOGE("Ethernet disconnected for more than 60 minutes. Restarting");
            delay(500);
            rp2040.reboot();
        }
    }

    return 1000;
}
