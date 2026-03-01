# Firmware Architecture

The firmware runs on an RP2040 dual-core microcontroller. Each core has a dedicated role: **Core 0** handles networking, the web API, and system housekeeping, while **Core 1** handles real-time energy data collection and storage.

```mermaid
flowchart TD
    subgraph BOOT["Boot Sequence (Core 0)"]
        direction TB
        B1[Init Serial & LEDs] --> B2[Init SD Card]
        B2 --> B3[Init RTC → set system time]
        B3 --> B4[Load config from SD]
        B4 --> B5[syncDeviceInfo]
        B5 --> B6[Init Ethernet / W5500]
        B6 --> B7[Init Datalog]
        B7 --> B8[Init Modbus RTU / RS-485]
        B8 --> B9[Setup Web API & HTTP Server]
        B9 --> B10[Register Core 0 tasks]
        B10 --> B11[Signal Core 1 via FIFO]
    end

    subgraph C0["Core 0 — Networking & Control"]
        direction TB

        subgraph C0LOOP["loop()  —  runs continuously"]
            direction TB
            L0A[server.handleClient\nServe HTTP API requests]
            L0B[handleButtonPress\nDebounce & queue addDeviceFromButton]
            L0C[c0Queue.runNextTask\nRun next scheduled task]
            L0A --> L0B --> L0C
        end

        subgraph C0TASKS["Core 0 Task Queue"]
            direction TB
            T0A["⏱ timeSync  (every 60s / 5s)\nNTP sync via UDP\nUpdate RTC & system clock\nReboot after 42 days"]
            T0B["⏱ checkEthernet  (every 1s)\nMonitor link/IP status\nLog connect/disconnect\nReboot after 60 min offline"]
            T0C["⏱ syncState  (every 1s)\nCheck SD Card health\nCheck Ethernet link\nUpdate LED colour\n🔴 SD error · 🟠 No link · 🟢 OK"]
            T0D["▶ addDeviceFromButton  (on demand)\nFind free Modbus address\nCreate deviceInfo entry\nSave config to SD\nQueue Assign action"]
        end

        C0LOOP --> C0TASKS
    end

    subgraph C1["Core 1 — Data Collection & Logging"]
        direction TB

        subgraph C1INIT["setup1()"]
            S1[Wait for FIFO signal from Core 0] --> S2[initLogData — set baseline timestamp]
            S2 --> S3[Start watchdog timer 800 ms]
        end

        subgraph C1LOOP["loop1()  —  1 s cycle"]
            direction TB
            L1A["collect()\nPoll every enabled inputDevice\nover Modbus RTU / RS-485\nDecode V, A, W, VA, PF, Hz\nUpdate device.current bucket\nUpdate Prometheus metrics"]
            L1B[c1Queue.runNextTask\nRun next scheduled task]
            L1C[rp2040.wdt_reset\nKick watchdog]
            L1A --> L1B --> L1C
        end

        subgraph C1TASKS["Core 1 Task Queue"]
            direction TB
            T1A["⏱ logData  (every interval)\nAccumulate Wh, VAh, VoltHrs\nWrite logRecord to SD datalog\nSkip if RTC not running"]
            T1B["⏱ syncDevices  (every 1s)\nAcquire mutex → syncDeviceInfo\n(apply config changes)\nAcquire mutex → syncDeviceAction\n(promote control→data)\nAcquire mutex → syncDeviceData\n(copy live readings to shared struct)"]
            T1C["⏱ deviceActionTask  (every 1s)\nRead deviceActionData\nLocate: flash device LED via Modbus\nAssign: set Modbus address on device"]
        end

        C1INIT --> C1LOOP
        C1LOOP --> C1TASKS
    end

    subgraph SHARED["Shared State  (mutex-protected)"]
        direction LR
        M1["deviceDataMu\ninputDeviceData[15]\n(V, A, PF, Hz — read by API)"]
        M2["deviceActionMu\ndeviceActionControl / Data\n(C0 writes, C1 executes)"]
        M3["deviceInfoMu\ninputDeviceInfo[15]\ndevicesChanged flag"]
        M4["sdMu\nSdFs  (SD Card)"]
    end

    subgraph HW["Hardware Peripherals"]
        direction LR
        HW1[W5500 Ethernet SPI]
        HW2[RS-485 Modbus RTU\nSerial1]
        HW3[SD Card SDIO]
        HW4[PCF85063A RTC\nI²C / Wire1]
        HW5[LED Red / Green]
        HW6[Physical Button]
    end

    BOOT --> C0
    BOOT --> C1

    C0LOOP -- "reads deviceData" --> M1
    C1TASKS -- "writes deviceData" --> M1

    C0LOOP -- "writes deviceActionControl" --> M2
    C1TASKS -- "reads / clears deviceActionData" --> M2

    C0LOOP -- "writes deviceInfos\ndevicesChanged" --> M3
    C1TASKS -- "reads deviceInfos" --> M3

    C0TASKS -- "SD reads/writes" --> M4
    C1TASKS -- "SD writes (datalog)" --> M4

    C0LOOP --> HW1
    C1LOOP --> HW2
    M4 --> HW3
    BOOT --> HW4
    C0TASKS -- "controls" --> HW5
    C0LOOP -- "reads" --> HW6
```

## Core Responsibilities

| | Core 0 | Core 1 |
|---|---|---|
| **Main loop focus** | HTTP request handling & button debounce | Energy data collection via Modbus (1 s cycle) |
| **Scheduled tasks** | `timeSync`, `checkEthernet`, `syncState` | `logData`, `syncDevices`, `deviceActionTask` |
| **On-demand tasks** | `addDeviceFromButton` (button press) | — |
| **Watchdog** | — | 800 ms watchdog, kicked each collection cycle |
| **LED control** | `syncState` sets colour, `Ticker` blinks at 1 Hz | — |

## Task Priorities (higher = runs first when multiple tasks are due)

| Task | Core | Priority |
|---|---|---|
| `logData` | 1 | 7 |
| `syncDevices` | 1 | 6 |
| `timeSync` | 0 | 5 |
| `deviceActionTask` | 1 | 5 |
| `checkEthernet` | 0 | 5 |
| `syncState` | 0 | 4 |

