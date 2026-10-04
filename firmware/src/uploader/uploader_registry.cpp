//
// Created by Nicholas Wiersma on 2026/08/20.
//

#include "auramon.h"
#include "uploader/homeassistant_uploader.h"
#include "uploader/influxdb2_uploader.h"
#include "uploader/uploader.h"
#include "uploader/uploader_registry.h"

namespace {
    Uploader *uploaderInstances[MAX_UPLOADERS] = {};

    // Lowest priority task on Core0: an uploader must never hold up SD
    // writes, Ethernet housekeeping, or syncState's LED/health check, all of
    // which take priority over pushing data to a remote service.
    constexpr uint8_t UPLOADER_TASK_PRIORITY = 2;

    void destroySlot(int slot) {
        if (uploaderInstances[slot]) {
            uploaderInstances[slot]->requestStop();
            uploaderInstances[slot] = nullptr;
        }
    }

    // Instantiates the concrete uploader implementation for a configured
    // type. Add one branch per uploader as it is implemented; unknown types
    // are logged and left unconfigured.
    Uploader *createUploader(const UploaderConfig &cfg) {
        /******************************************************************/
        /*********************** Add new uploaders below *******************/
        /******************************************************************/

        if (cfg.type == "influxdb2") {
            return new InfluxDB2Uploader(cfg.id);
        }
        if (cfg.type == "homeassistant") {
            return new HomeAssistantUploader(cfg.id);
        }

        return nullptr;
    }

    void syncSlot(int slot) {
        UploaderConfig *cfg = uploaderConfigs[slot];

        if (!cfg || !cfg->enabled) {
            destroySlot(slot);
            return;
        }

        if (!uploaderInstances[slot]) {
            Uploader *u = createUploader(*cfg);
            if (!u) {
                LOGE("uploader %s: unknown type '%s'", cfg->id.c_str(), cfg->type.c_str());
                return;
            }
            if (!u->configure(*cfg)) {
                delete u;
                return;
            }

            uploaderInstances[slot] = u;
            c0Queue.add([u](void *) { return u->dispatch(); }, UPLOADER_TASK_PRIORITY);
            return;
        }

        if (!uploaderInstances[slot]->configure(*cfg)) {
            LOGE("uploader %s: configuration rejected, stopping", cfg->id.c_str());
            destroySlot(slot);
        }
    }
}

uint32_t syncUploaders(void *param) {
    (void) param;

    syncUploaderInstances(false);

    return 1000;
}

void syncUploaderInstances(bool force) {
    if (!mutex_enter_timeout_ms(&uploaderConfigMu, 100)) {
        LOGE("syncUploaderInstances: could not acquire uploaderConfigMu");
        return;
    }

    if (force || uploadersChanged) {
        for (int i = 0; i < MAX_UPLOADERS; i++) {
            syncSlot(i);
        }
        uploadersChanged = false;
    }

    mutex_exit(&uploaderConfigMu);
}
