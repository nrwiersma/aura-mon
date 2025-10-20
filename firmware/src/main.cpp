#include "auramon.h"

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);

RTC_PCF8563 rtc;

mutex_t sdMu;
SdFs sd;
volatile bool sdRunning = false;

ModbusRTUMaster modbus(Serial1, RS485_DE);

inputDevice *devices[MAX_DEVICES] = {};

logger msgLog;

extern void waitForSerial();

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);

    waitForSerial();

    LOGD("Booting core 0");

    // Wait for the SD Card to be initialised.
    while (!sdRunning) {
        delay(1);
    }

    // Start the Ethernet port.
    eth.setSPISpeed(ETH_FREQ);
    eth.hostname("aura-mon");
    if (!eth.begin(mac)) {
        LOGE("No wired Ethernet hardware detected. Check pinouts, wiring.");

        while (true) { delay(1000); }
    }

    if (!rtc.begin(&Wire1)) {
        LOGE("No RTC detected");
    }
    if (rtc.isrunning()) {
        if (rtc.lostPower()) {
            LOGI("RTC lost power. Please check your battery");
        }
        DateTime dt = rtc.now();
        time_t ts = dt.unixtime();
        struct timeval tv;
        tv.tv_sec = ts;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        LOGI("RTC is running: Unix time %d", ts);
    } else {
        LOGI("RTC not running");
    }
}

void loop() {
    delay(100);
}

void setup1() {
    waitForSerial();

    LOGD("Booting core 1");

    mutex_init(&sdMu);
    if (!sd.begin(SD_CONFIG)) {
        Serial.println("Could not initialize SD Card. Halting");
        sd.initErrorPrint(&Serial);

        while (1) { delay(1000); }
    }
    sdRunning = true;

    LOGI("Initialized SD Card!");

    // mutex_enter_timeout_ms(&sdMu, 500);
    // FsFile fTest = sd.open("/test.txt");
    // String str = fTest.readString();
    // fTest.close();
    // mutex_exit(&sdMu);
    //
    // info("Got data from file: %s", str.c_str());

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);
    modbus.setTimeout(1000);

    // TODO: temp until I have config.
    devices[0] = new inputDevice(1);
    devices[0]->enabled = true;
    devices[1] = new inputDevice(2);
    devices[1]->enabled = true;

    LOGI("Modbus initialised");
}

void loop1() {
    const unsigned long start = millis();

    collect();

    // The data only refreshes every second. Wait for the next run.
    const unsigned long waitTimeMs = 1000 - (millis() - start);
    delay(waitTimeMs);
}

void waitForSerial() {
    if (WAIT_FOR_SERIAL) {
        while (!Serial) { delay(100); }
    }
}
