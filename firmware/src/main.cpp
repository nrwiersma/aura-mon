#include "auramon.h"

mutex_t sdMu;
SdFs    sd;

logger msgLog;

PCF85063A rtc;
bool      rtcRunning = false;

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);

mutex_t deviceInfoMu;
inputDeviceInfo *deviceInfos[MAX_DEVICES] = {};
inputDevice *devices[MAX_DEVICES] = {};
dataLog      datalog;

ModbusRTUMaster modbus(Serial1, RS485_DE);

WebServer server(80);

taskQueue c0Queue = {};
taskQueue c1Queue = {};

volatile bool setupComplete = false;

void waitForSerial();

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);

    waitForSerial();

    LOGD("Booting");

    mutex_init(&sdMu);
    if (!sd.begin(SD_CONFIG)) {
        Serial.println("Could not initialize SD Card. Halting");
        sd.initErrorPrint(&Serial);

        while (1) { delay(1000); }
    }

    LOGI("SD Card initialised");

    rtc.begin(&Wire1);

    LOGI("RTC initialised");

    if (rtc.isRunning()) {
        if (rtc.lostPower()) {
            LOGI("RTC lost power. Please check your battery");
        }
        time_t         ts = rtc.now();
        struct timeval tv;
        tv.tv_sec = ts;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        rtcRunning = true;
        LOGI("RTC is running: Unix time %d", ts);
    } else {
        LOGI("RTC not running");
    }

    eth.setSPISpeed(ETH_FREQ);
    eth.hostname("aura-mon");
    if (!eth.begin(mac)) {
        LOGE("No wired Ethernet hardware detected.");

        while (true) { delay(1000); }
    }

    LOGI("Ethernet initialised");

    if (!datalog.begin()) {
        LOGE("Datalog could not be opened.");

        while (true) { delay(1000); }
    }

    LOGI("Datalog initialised");

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);
    modbus.setTimeout(100);

    LOGI("Modbus initialised");

    // TODO: temp until I have config.
    mutex_init(&deviceInfoMu);
    // TODO: sync into deviceInfos from config.
    devices[0] = new inputDevice(1);
    devices[0]->name = const_cast<char *>("test1");
    devices[0]->enabled = true;
    devices[1] = new inputDevice(2);
    devices[1]->name = const_cast<char *>("test2");
    devices[1]->enabled = true;

    setupAPI();
    server.begin();

    c0Queue.add(timeSync, 5);
    c0Queue.add(checkEthernet, 5);

    c1Queue.add(logData, 7);

    setupComplete = true;
}

void loop() {
    server.handleClient();

    if (!c0Queue.runNextTask()) {
        delay(10);
    }
}

void setup1() {
    while (!setupComplete) {
        delay(10);
    }

    // Set the initial monotonic ts so we know
    // how long it took before the first log write.
    initLogData();
}

void loop1() {
    const unsigned long start = millis();

    collect();

    // Run any available tasks until collection is ready.
    while (millis() - start < 1000) {
        if (!c1Queue.runNextTask()) {
            delay(10);
        }
    }
}

void waitForSerial() {
    if constexpr (WAIT_FOR_SERIAL) {
        while (!Serial) { delay(100); }
    }
}
