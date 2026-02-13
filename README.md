<picture>
  <source media="(prefers-color-scheme: dark)" srcset="http://svg.wiersma.co.za/github/project.v2?title=aura mon&tag=energy%20data%20logger&mode=dark">
  <source media="(prefers-color-scheme: light)" srcset="http://svg.wiersma.co.za/github/project.v2?title=aura mon&tag=energy%20data%20logger">
  <img alt="Logo" src="http://svg.wiersma.co.za/github/project.v2?title=aura mon&tag=energy%20data%20logger">
</picture>

![board image](assets/board.png)

[![GitHub release](https://img.shields.io/github/release/nrwiersma/aura-mon.svg)](https://github.com/nrwiersma/aura-mon/releases)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/nrwiersma/aura-mon/master/LICENSE)

`Aura Mon` is an energy datalogger specifically targeting the tiny [SPM01](https://www.bituo-technik.com/product/spm01-flexible-1pn-63a/).

Features:

* RP2350 microcontroller
* Ethernet for reliable network connectivity
* SDCard for storage
* RTC
* Modbus RTU over RS485

## Why

The goal of this project is to create a reliable and easy to use energy datalogger that can be used to monitor energy consumption in a home or small business.
The SPM01 is a very affordable energy meter that can measure voltage, current, power, energy and power factor. This reduces the space needed in small electrical panels,
making it ideal for home use.

## Powering

The board can be powered using an external 5V power supply connected to the screw terminals. For testing purposes, it can also be powered via USB-C, after connecting the onboard jumper.

## Firmware

The firmware is written in C++ using the [Arduino framework](https://www.arduino.cc/en/framework) and is available in the `firmware` folder. It is designed to specifically meet my initial needs,
but is open for contributions and improvements. The firmware is responsible for reading data from the SPM01, storing it on the SDCard, and serving it over HTTP.

**Note**: The firmware is still in development and is not yet ready for production use. It is currently only tested on the hardware I have, so there may be bugs and issues that need to be addressed.

### API

The firmware exposes an HTTP API that can be used to retrieve the stored data and configure the device. The API is documented in the `API.md` file.

## Changelog

#### v0.1.0

* Initial schematic and board
* Initial firmware with basic functionality to read from the SPM01 and store data on the SDCard
