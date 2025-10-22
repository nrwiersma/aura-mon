//
// Created by Nicholas Wiersma on 2025/10/22.
//

#ifndef FIRMWARE_PCF85063A_H
#define FIRMWARE_PCF85063A_H


#include "Arduino.h"
#include "Wire.h"

class PCF85063A {
public:
    PCF85063A(): _i2c(nullptr) {};

    void begin(TwoWire *wire);
    bool lostPower(void) const;
    bool isRunning(void) const;
    time_t now() const;
    void adjust(time_t time) const;
    void start(void) const;
    void stop(void) const;

private:
    TwoWire *_i2c;

    static uint8_t bin2bcd(uint8_t val);
    static uint8_t bcd2bin(uint8_t val);
    uint8_t readReg(uint8_t reg) const;
    void read(uint8_t reg, uint8_t *buf, size_t len) const;
    void writeReg(uint8_t reg, uint8_t val) const;
    void write(const uint8_t *buf, size_t len) const;
};

#endif //FIRMWARE_PCF85063A_H
