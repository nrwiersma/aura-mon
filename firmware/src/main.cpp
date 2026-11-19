//
// Created by Nicholas Wiersma on 2026/03/19.
//

#include "auramon.h"

time_t            startTime;
Ticker            ledTimer;
volatile LEDColor ledState;

SdFs sd;

Logger msgLog;

PCF85063A rtc;
bool      rtcRunning = false;

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);
NetworkConfig  netCfg;

DeviceActionRequest deviceAction = {DeviceActionType::None, 0};
InputDevice *       devices[MAX_DEVICES] = {};
DataLog             datalog;

promMetrics metrics;

bool            uploadersChanged = false;
UploaderConfig *uploaderConfigs[MAX_UPLOADERS] = {};

ModbusRTUMaster modbus(Serial1, RS485_DE);
static uint32_t lastCollect = 0;

WebServer server(80);
bool configChanged = false;

taskQueue queue = {};

void blinkLED();
void handleButtonPress();
void waitForSerial();

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    waitForSerial();

    LOGD("Booting");

    if (!sd.begin(SD_CONFIG)) {
        Serial.println("Could not initialize SD Card. Halting");
        sd.initErrorPrint(&Serial);

        digitalWrite(LED_GREEN, LOW);
        while (1) { delay(1000); }
    }

    LOGI("Firmware version: %s", AURAMON_VERSION);
    restartReasonLog();
    LOGI("SD Card initialised");

    rtc.begin(&Wire1);

    LOGI("RTC initialised");

    if (rtc.isRunning()) {
        if (rtc.lostPower()) {
            LOGI("RTC lost power. Please check your battery");
        }
        time_t  ts = rtc.now();
        timeval tv;
        tv.tv_sec = ts;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        rtcRunning = true;
        LOGI("RTC is running: Unix time %" PRId64, (int64_t)ts);
    } else {
        LOGI("RTC not running");
    }
    startTime = time(nullptr);

    if (auto err = loadConfig(); err) {
        LOGI("Could not load config from SD Card: %s", err.Error());
    } else {
        LOGI("Config loaded from SD Card");
    }
    syncUploaderInstances(true);

    eth.setSPISpeed(ETH_FREQ);
    eth.hostname(netCfg.hostname);
    if (netCfg.hasIP()) {
        eth.config(
            ipaddr_addr(netCfg.ip.c_str()),
            ipaddr_addr(netCfg.gateway.c_str()),
            ipaddr_addr(netCfg.mask.c_str()),
            ipaddr_addr(netCfg.dns.c_str()),
            0);
    }
    if (!eth.begin(mac)) {
        LOGE("No wired Ethernet hardware detected.");

        digitalWrite(LED_GREEN, LOW);
        while (true) { delay(1000); }
    }

    LOGI("Ethernet initialised");

    if (!datalog.begin()) {
        LOGE("Datalog could not be opened.");

        digitalWrite(LED_GREEN, LOW);
        while (true) { delay(1000); }
    }

    LOGI("Datalog initialised");

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);
    modbus.setTimeout(60);

    LOGI("Modbus initialised");

    setupAPI();
    server.begin();

    queue.add(timeSync, 5);
    queue.add(checkEthernet, 5);
    queue.add(syncState, 4);
    queue.add(syncUploaders, 3);
    queue.add(logData, 7);
    queue.add(deviceActionTask, 5);

    syncState(nullptr);
    ledTimer.attach(1, blinkLED);

    initLogData();

    wdtBegin(6000);
}

void loop() {
    // Check if the button was pressed.
    handleButtonPress();

    // TODO: Consider if the collect can be split further down the line.
    if (millis() - lastCollect >= 1000 || lastCollect == 0) {
        lastCollect = millis();
        collect();
    }

    // Serve any web requests.
    server.handleClient();
    wdtFeed();

    // If there is a task ready in the queue, run it.
    queue.runNextTask();
    wdtFeed();
}

void safeReboot() {
    // Drain the data log.
    datalog.end();

    // Block any remaining SD operations and cleanly terminate the
    // SDIO peripheral before resetting.
    sd.end();
    rp2040.reboot();
}

void blinkLED() {
    static bool on = false;
    switch (ledState) {
        case LEDColor::Red:
            digitalWrite(LED_RED, on ? HIGH : LOW);
            digitalWrite(LED_GREEN, LOW);
            break;
        case LEDColor::Green:
            digitalWrite(LED_RED, LOW);
            digitalWrite(LED_GREEN, on ? HIGH : LOW);
            break;
        case LEDColor::Orange:
            digitalWrite(LED_RED, on ? HIGH : LOW);
            digitalWrite(LED_GREEN, on ? HIGH : LOW);
            break;
    }
    on = !on;
}

void handleButtonPress() {
    static int           lastReading = HIGH;
    static int           stableState = HIGH;
    static unsigned long lastChangeMs = 0;

    int reading = digitalRead(BUTTON_PIN);
    if (reading != lastReading) {
        lastChangeMs = millis();
        lastReading = reading;
    }

    if (millis() - lastChangeMs < BUTTON_DEBOUNCE_MS) {
        return;
    }

    if (stableState == reading) {
        return;
    }

    stableState = reading;
    if (stableState == HIGH) {
        queue.add(addDeviceFromButton, 6);
    }
}

void waitForSerial() {
    if constexpr (WAIT_FOR_SERIAL) {
        while (!Serial) { delay(100); }
    }
}
