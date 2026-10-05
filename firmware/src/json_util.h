//
// Created by Nicholas Wiersma on 2026/10/04.
//

#pragma once

#include <Arduino.h>

// Copies a potentially-unaligned const char* (e.g. from an ArduinoJSON pool) into
// a 4-byte-aligned stack buffer byte-by-byte, then assigns that buffer to dest.
// This avoids the rp2350-memcpy.S crash that occurs when memcpy is given an
// unaligned source pointer. Strings longer than 255 characters are truncated.
inline void assignFromJson(String &dest, const char *src) {
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
