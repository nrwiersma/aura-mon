//
// Created by Nicholas Wiersma on 2025/09/25.
//

#include "auramon.h"

const inline char *modbusErrStr[] PROGMEM = {
    "success",
    "invalid id",
    "invalid buffer",
    "invalid quantity",
    "response timeout",
    "frame error",
    "crc error",
    "unknown comm error",
    "unexpected id",
    "exception response",
    "unexpected function code",
    "unexpected response length",
    "unexpected byte count",
    "unexpected address",
    "unexpected value",
    "unexpected quantity"
};

const inline char * PROGMEM modbusExcpStr[] = {
    "illegal function",
    "illegal data address",
    "illegal data value",
    "slave device failure",
    "acknowledge",
    "slave device busy",
    "negative acknowledge",
    "memory parity error",
    "gateway path unavailable",
    "gateway target device failed to respond"
};

const char *modbusError(uint8_t err) {
    if (err == MODBUS_RTU_MASTER_EXCEPTION_RESPONSE) {
        return modbusExcpStr[modbus.getExceptionResponse()];
    }
    return modbusErrStr[err];
}

void assignModbusAddress(uint16_t id) {
    Serial1.begin(9600);
    modbus.begin(9600);

    delay(100);

    if (const uint8_t err = modbus.writeSingleHoldingRegister(247, 0x1771, 5)) {
        LOGE("Could not set baudrate: %s", modbusError(err));
    }

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);

    delay(100);

    uint16_t modbusAddress[] = {0x55AA, id};
    if (const uint8_t err = modbus.writeMultipleHoldingRegisters(0, 0x7530, modbusAddress, 2)) {
        LOGE("Could not write modbus address: %s", modbusError(err));
    }
}
