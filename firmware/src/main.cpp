#include "auramon.h"

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);

RTC_PCF8563 rtc;

mutex_t sdMu;
SdFs sd;

ModbusRTUMaster modbus(Serial1, RS485_DE);

inputDevice * *devices = new inputDevice *[MAX_DEVICES];

logger msgLog;

extern void waitForSerial();

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    waitForSerial();

    info("Booting");

    // Start the Ethernet port.
    // eth.setSPISpeed(33);
    eth.hostname("aura-mon");
    if (!eth.begin(mac)) {
        error("No wired Ethernet hardware detected. Check pinouts, wiring.");

        while (true) { delay(1000); }
    }

    // Wait until IP was acquired.
    while (!eth.connected()) {
        // Serial.println("Waiting for DHCP address..");
        delay(500);
    }

    info("Ethernet Ready!");
    info("IP address: %s", eth.localIP().toString().c_str());

    if (!rtc.begin(&Wire1)) {
        error("No RTC detected");
        while (true) { delay(1000); }
    }
    if (rtc.isrunning()) {
        info("Running RTC detected");
        if (!rtc.lostPower()) {
            DateTime dt = rtc.now();

            if (time(nullptr) < 10000000) {
                struct timeval tv;
                tv.tv_sec = dt.unixtime();
                tv.tv_usec = 0;
                settimeofday(&tv, nullptr);

                info("Time set from RTC");
            } else {
                info("Time already set from NTP");
            }
        } else {
            info("RTC lost power. Time cannot be used");
        }
    } else {
        info("RTC is not running. Waiting for time sync");
    }
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);

    delay(1000);

    digitalWrite(LED_BUILTIN, LOW);

    delay(500);
}

void setup1() {
    waitForSerial();

    mutex_init(&sdMu);
    if (!sd.begin(SD_CONFIG)) {
        error("Failed to initialize SD Card");
        // TODO: figure out how to get an error string.
        // sd.initErrorPrint(&Serial);
        // while (1) { delay(1000); }
    }

    info("Initialized SD Card!");

    mutex_enter_timeout_ms(&sdMu, 500);
    FsFile fTest = sd.open("/test.txt");
    String str = fTest.readString();
    fTest.close();
    mutex_exit(&sdMu);

    info("Got data from file: %s", str.c_str());

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);
    modbus.setTimeout(1000);

    // if (const uint8_t err = modbus.writeSingleHoldingRegister(2, 0x2710, 0x5055)) {
    //     Serial.print("Could not set locating: ");
    //     Serial.println(modbusError(err));
    // }

    // assignModbusAddress(2);

    // TODO: temp until I have config.
    devices[0] = new inputDevice(1);
    devices[0]->enabled = true;
    devices[1] = new inputDevice(2);
    devices[1]->enabled = true;

    info("Modbus initialised");
}

void loop1() {
    const unsigned long start = millis();

    collect();

    // The data only refreshes every second. Wait for the next run.
    const unsigned long waitTimeMs = 1000 - (millis() - start);
    delay(waitTimeMs);
}

void waitForSerial() {
    delay(1000);
    if (DEBUG) {
        while (!Serial) { delay(100); }
    }
}
