# Firmware Architecture

The firmware runs on an RP2040 dual-core microcontroller. Each core has a dedicated role: **Core 0 (Control Plane)** handles networking, the web API, and system housekeeping, while **Core 1 (Data Plane)** handles real-time energy data collection and storage.

```mermaid
flowchart TD
    subgraph BOOT["Boot Sequence (Core 0)"]
        direction TB
        B1[Init Serial & LEDs] --> B2[Init SD Card]
        B2 --> B3[Init RTC & set system time]
        B3 --> B4[Load config & sync device info]
        B4 --> B5[Init Ethernet / W5500]
        B5 --> B6[Init Datalog]
        B6 --> B7[Init Modbus RTU / RS-485]
        B7 --> B8[Setup Web API & HTTP server]
        B8 --> B9[Register tasks & signal Core 1 via FIFO]
    end

    subgraph C0["Core 0 — Control Plane"]
        direction TB
        L0A[server.handleClient\nServe HTTP API requests]
        L0B[handleButtonPress\nDebounce button & queue addDeviceFromButton]
        L0C[c0Queue.runNextTask]

        T0A["⏱ timeSync  · 60s\nNTP sync, update RTC\nReboot after 42 days"]
        T0B["⏱ checkEthernet  · 1s\nMonitor link & IP\nReboot after 60 min offline"]
        T0C["⏱ syncState  · 1s\nCheck SD & Ethernet\nUpdate LED  🔴 · 🟠 · 🟢"]
        T0D["▶ addDeviceFromButton  · on demand\nAssign free Modbus address\nSave config to SD"]
        T0E["⏱ writeLogData  · 100ms idle\nPop queued records\nWrite record to SD datalog"]

        L0C --> T0A & T0B & T0C & T0D & T0E
    end

    subgraph C1["Core 1 — Data Plane"]
        direction TB
        S1[Wait for FIFO signal] --> S2[initLogData & start watchdog 800ms]

        L1A["collect()  · each cycle\nPoll all devices over Modbus RTU\nDecode V, A, W, VA, PF, Hz\nUpdate metrics"]
        L1B[c1Queue.runNextTask\nKick watchdog]

        T1A["⏱ logData  · per interval\nAccumulate Wh / VAh / VoltHrs\nQueue record for Core 0"]
        T1B["⏱ syncDevices  · 1s\nSync device info, actions\n& live readings via mutexes"]
        T1C["⏱ deviceActionTask  · 1s\nLocate: flash device LED\nAssign: set Modbus address"]

        S2 --> L1A --> L1B
        L1B --> T1A & T1B & T1C
    end

    BOOT --> C0 & C1
```

## Core Responsibilities

| | Core 0 | Core 1 |
|---|---|---|
| **Main loop focus** | HTTP request handling & button debounce | Energy data collection via Modbus (1 s cycle) |
| **Scheduled tasks** | `timeSync`, `checkEthernet`, `syncState`, `writeLogData` | `logData`, `syncDevices`, `deviceActionTask` |
| **On-demand tasks** | `addDeviceFromButton` (button press) | — |
| **Watchdog** | — | 800 ms watchdog, kicked each collection cycle |
| **LED control** | `syncState` sets colour, `Ticker` blinks at 1 Hz | — |

## Task Priorities (higher = runs first when multiple tasks are due)

| Task | Core | Priority |
|---|---|---|
| `logData` | 1 | 7 |
| `syncDevices` | 1 | 6 |
| `writeLogData` | 0 | 6 |
| `timeSync` | 0 | 5 |
| `deviceActionTask` | 1 | 5 |
| `checkEthernet` | 0 | 5 |
| `syncState` | 0 | 4 |

## Data Log Record Queue

`logData` on Core 1 accumulates energy readings, but does not touch the SD card. Once an
interval completes, the record is copied onto a fixed size FIFO queue (`logQueue`, 8 records)
and `writeLogData` on Core 0 pops it and writes it to the data log. This keeps SD card IO,
which can block for tens of milliseconds, off the collection core.

- If the queue is full, `logData` retries on the next cycle rather than dropping the record,
  so the log slips but no data is lost.
- Records are queued and written in timestamp order, which the data log requires.
- A record that fails to write is retried up to 5 times, then dropped so it cannot block the
  records behind it.
- On a controlled reboot, the queue is drained before the data log is closed.

## Hardware Peripherals

| Peripheral | Interface | Used By |
|---|---|---|
| W5500 Ethernet | SPI | Control Plane — HTTP server, NTP, link monitoring |
| RS-485 / Modbus RTU | Serial1 | Data Plane — device polling & address assignment |
| SD Card | SDIO (`sdMu`) | Control Plane — config r/w & datalog writes |
| PCF85063A RTC | I²C / Wire1 | Boot — set system clock |
| LED (Red / Green) | GPIO 10 / 11 | Control Plane — `syncState` sets colour, `Ticker` blinks at 1 Hz |
| Physical Button | GPIO 3 | Control Plane — triggers `addDeviceFromButton` on press |

