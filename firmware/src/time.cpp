//
// Created by Nicholas Wiersma on 2025/10/01.
//

#include "auramon.h"

uint32_t syncTime(void *param) {
    (void) param;

    LOGD("sync time ran");

    return 1000;
}
