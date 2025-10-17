//
// Created by Nicholas Wiersma on 2025/10/01.
//

#include "auramon.h"
#include <task.h>

void syncTime(void *param) {
    (void) param;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
