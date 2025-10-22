//
// Created by Nicholas Wiersma on 2025/10/22.
//

#include "PCF85063A.h"

#define PCF85063_ADDRESS 0x51
#define PCF85063_CONTROL_1 0x00
#define PCF85063_VL_SECONDS 0x04

// Datetime <-> Unix time comes from https://github.com/adafruit/RTClib.
#define SECONDS_FROM_1970_TO_2000 946684800

const uint8_t daysInMonth[] PROGMEM = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30
};

static uint16_t date2days(uint16_t y, uint8_t m, uint8_t d) {
    if (y >= 2000U)
        y -= 2000U;
    uint16_t days = d;
    for (uint8_t i = 1; i < m; ++i)
        days += pgm_read_byte(daysInMonth + i - 1);
    if (m > 2 && y % 4 == 0)
        ++days;
    return days + 365 * y + (y + 3) / 4 - 1;
}

static uint32_t time2ulong(uint16_t days, uint8_t h, uint8_t m, uint8_t s) {
    return ((days * 24UL + h) * 60 + m) * 60 + s;
}

void PCF85063A::begin(TwoWire *i2c) {
    _i2c = i2c;
    _i2c->begin();
}

bool PCF85063A::lostPower(void) const {
    return readReg(PCF85063_VL_SECONDS) >> 7;
}

bool PCF85063A::isRunning(void) const {
    return !((readReg(PCF85063_CONTROL_1) >> 5) & 1);
}

time_t PCF85063A::now() const {
    uint8_t buf[7];
    read(PCF85063_VL_SECONDS, buf, 7);

    auto yOff = bcd2bin(buf[6]);
    auto m = bcd2bin(buf[5]) & 0x1F;
    auto d = bcd2bin(buf[3]) & 0x3F;
    auto hh = bcd2bin(buf[2]) & 0x3F;
    auto mm = bcd2bin(buf[1]) & 0x7F;
    auto ss = bcd2bin(buf[0]) & 0x7F;

    uint32_t t;
    uint16_t days = date2days(yOff, m, d);
    t = time2ulong(days, hh, mm, ss);
    t += SECONDS_FROM_1970_TO_2000; // seconds from 1970 to 2000

    return t;
}

void PCF85063A::adjust(time_t t) const {
    t -= SECONDS_FROM_1970_TO_2000; // bring to 2000 timestamp from 1970
    uint8_t ss = t % 60;
    t /= 60;
    uint8_t mm = t % 60;
    t /= 60;
    uint8_t hh = t % 24;
    uint16_t days = t / 24;
    uint8_t leap;
    uint8_t yOff;
    for (yOff = 0;; ++yOff) {
        leap = yOff % 4 == 0;
        if (days < 365U + leap)
            break;
        days -= 365 + leap;
    }
    uint8_t m;
    for (m = 1; m < 12; ++m) {
        uint8_t daysPerMonth = pgm_read_byte(daysInMonth + m - 1);
        if (leap && m == 2)
            ++daysPerMonth;
        if (days < daysPerMonth)
            break;
        days -= daysPerMonth;
    }
    uint8_t d = days + 1;

    uint8_t buf[8] = {
        PCF85063_VL_SECONDS, // start at location 4, VL_SECONDS
        bin2bcd(ss), bin2bcd(mm),
        bin2bcd(hh), bin2bcd(d),
        bin2bcd(0), // skip weekdays
        bin2bcd(m), bin2bcd(yOff)
    };
    write(buf, 8);
}

void PCF85063A::start(void) const {
    uint8_t ctlreg = readReg(PCF85063_CONTROL_1);
    if (ctlreg & (1 << 5)) {
        writeReg(PCF85063_CONTROL_1, ctlreg & ~(1 << 5));
    }
}

void PCF85063A::stop(void) const {
    uint8_t ctlreg = readReg(PCF85063_CONTROL_1);
    if (ctlreg & (1 << 5)) {
        writeReg(PCF85063_CONTROL_1, ctlreg | (1 << 5));
    }
}

uint8_t PCF85063A::bin2bcd(uint8_t val) {
    return val + 6 * (val / 10);
}

uint8_t PCF85063A::bcd2bin(uint8_t val) {
    return val - 6 * (val >> 4);
}

uint8_t PCF85063A::readReg(const uint8_t reg) const {
    _i2c->beginTransmission(PCF85063_ADDRESS);
    _i2c->write(reg);
    if (_i2c->endTransmission() != 0) {
        return 0;
    }
    _i2c->requestFrom(PCF85063_ADDRESS, 1);
    return _i2c->read();
}

void PCF85063A::read(const uint8_t reg, uint8_t *buf, const size_t len) const {
    _i2c->beginTransmission(PCF85063_ADDRESS);
    _i2c->write(reg);
    _i2c->endTransmission();

    _i2c->requestFrom(PCF85063_ADDRESS, len);
    for (size_t i = 0; i < len; i++) {
        buf[i] = _i2c->read();
    }
}

void PCF85063A::writeReg(const uint8_t reg, const uint8_t val) const {
    _i2c->beginTransmission(PCF85063_ADDRESS);
    _i2c->write(reg);
    _i2c->write(val);
    _i2c->endTransmission();
}

void PCF85063A::write(const uint8_t *buf, const size_t len) const {
    _i2c->beginTransmission(PCF85063_ADDRESS);
    _i2c->write(buf, len);
    _i2c->endTransmission();
}
