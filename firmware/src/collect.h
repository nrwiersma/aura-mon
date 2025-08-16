//
// Created by Nicholas Wiersma on 2025/09/22.
//

#ifndef FIRMWARE_SAMPLE_H
#define FIRMWARE_SAMPLE_H

void collect();
uint8_t readFrame(inputDevice* device);
float float_abcd(uint16_t hi, uint16_t lo);

#endif //FIRMWARE_SAMPLE_H