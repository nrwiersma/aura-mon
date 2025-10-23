//
// Created by Nicholas Wiersma on 2025/10/23.
//

#include "auramon.h"

struct logRecordKey {
    uint32_t rev;
    uint32_t ts;
};

int8_t log::begin() {
    if (_file) return 0;

    mutex_enter_blocking(&sdMu);
    if (!sd.exists(DATA_LOG_PATH)) {
        String msgDir = DATA_LOG_PATH;
        msgDir.remove(msgDir.indexOf('/', 1));
        sd.mkdir(msgDir.c_str());
    }
    _file = sd.open(DATA_LOG_PATH);
    if (!_file) {
        return 1;
    }

    _fileSize = _physicalSize = _file.size();

    mutex_exit(&sdMu);

}

