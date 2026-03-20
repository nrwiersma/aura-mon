//
// Created by Nicholas Wiersma on 2025/09/24.
//

#pragma once

const char *modbusError(uint8_t err);
void        locateModbusDevice(uint16_t id);
void        assignModbusAddress(uint16_t id);
