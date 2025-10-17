#include "auramon.h"
#include <SdFat.h>
#include <task.h>

SdFs sd;
SyncFS syncFS;

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);

RTC_PCF8563 rtc;

ModbusRTUMaster modbus(Serial1, RS485_DE);

inputDevice *devices[MAX_DEVICES] = {};

logger msgLog;

static TaskHandle_t __collectTask;

extern void waitForSerial();

extern void initNTP();


void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);

    waitForSerial();

    LOGD("Booting");

    if (!sd.begin(SdioConfig(SD_CLK, SD_CMD, SD_DAT0))) {
        Serial.println("Could not initialize SD Card. Halting");
        sd.initErrorPrint(&Serial);

        while (1) { delay(1000); }
    }
    if (!syncFS.begin(sd)) {
        // Dont use logging here, it needs the SD Card.
        Serial.println("Could not initialize Sync FS. Halting");

        while (1) { delay(1000); }
    }

    LOGI("SD Card initialised");

    if (!rtc.begin(&Wire1)) {
        LOGE("No RTC detected");
    } else {
        LOGI("RTC initialised");
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
        LOGI("RTC not running")
    }

    // eth.setSPISpeed(40000000); // 40MHz.
    // eth.hostname("aura-mon");
    // if (!eth.begin(mac)) {
    //     LOGE("No wired Ethernet hardware detected. Halting");
    //
    //     while (true) { delay(1000); }
    // }

    LOGI("Ethernet initialised");

    // Wait until IP was acquired.
    // while (!eth.connected()) {
    //     // Serial.println("Waiting for DHCP address..");
    //     delay(500);
    // }
    //
    // info("Ethernet Ready!");
    // info("IP address: %s", eth.localIP().toString().c_str());

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);
    modbus.setTimeout(100);

    LOGI("Modbus initialised");

    // TODO: temp until I have config.
    devices[0] = new inputDevice(1);
    devices[0]->enabled = true;
    devices[1] = new inputDevice(2);
    devices[1]->enabled = true;

    xTaskCreate(collectTask, "collect", 512, NULL, configMAX_PRIORITIES - 1, &__collectTask);
    vTaskCoreAffinitySet(__collectTask, 1 << 1); // Core 1.
}

void loop() {
    // initNTP();

    delay(100);
}

void waitForSerial() {
    if (WAIT_FOR_SERIAL) {
        while (!Serial) { delay(100); }
    }
}

bool ntpRunning = false;

void initNTP() {
    if (ntpRunning || !eth.connected()) {
        return;
    }

    IPAddress s1, s2;
    eth.hostByName("pool.ntp.org", s1);
    eth.hostByName("time.nist.gov", s2);

    if (!s1.isSet() && !s2.isSet()) {
        LOGI("Can't resolve ntp servers");
        return;
    }

    sntp_stop();
    if (s1.isSet()) {
        sntp_setserver(0, s1);
    }
    if (s2.isSet()) {
        sntp_setserver(1, s2);
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_init();
    ntpRunning = true;
}
