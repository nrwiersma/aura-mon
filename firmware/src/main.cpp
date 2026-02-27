#include "auramon.h"

// Disable SysTick on core 1 to free up the timer for Ticker.
bool core1_disable_systick = true;
// Use a separate stack for core 1 to avoid clashes with core 0.
bool core1_separate_stack = true;

time_t            startTime;
Ticker            ledTimer;
volatile LEDColor ledState;

mutex_t sdMu;
SdFs    sd;

logger msgLog;

PCF85063A rtc;
bool      rtcRunning = false;

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);
NetworkConfig  netCfg;

mutex_t             deviceDataMu;
inputDeviceData *   deviceData[MAX_DEVICES] = {};
mutex_t             deviceActionMu;
deviceActionRequest deviceActionControl = {deviceActionType::None, 0};
deviceActionRequest deviceActionData = {deviceActionType::None, 0};
mutex_t             deviceInfoMu;
volatile bool       devicesChanged;
inputDeviceInfo *   deviceInfos[MAX_DEVICES] = {};
inputDevice *       devices[MAX_DEVICES] = {};
dataLog             datalog;

promMetrics metrics;

ModbusRTUMaster modbus(Serial1, RS485_DE);

WebServer server(80);

taskQueue c0Queue = {};
taskQueue c1Queue = {};

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

    mutex_init(&sdMu);
    if (!sd.begin(SD_CONFIG)) {
        Serial.println("Could not initialize SD Card. Halting");
        sd.initErrorPrint(&Serial);

        digitalWrite(LED_GREEN, LOW);
        while (1) { delay(1000); }
    }

    LOGI("Firmware version: %s", AURAMON_VERSION);
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
        LOGI("RTC is running: Unix time %d", ts);
    } else {
        LOGI("RTC not running");
    }
    startTime = time(nullptr);

    mutex_init(&deviceDataMu);
    mutex_init(&deviceActionMu);
    mutex_init(&deviceInfoMu);

    if (auto err = loadConfig(); err) {
        LOGI("Could not load config from SD Card: %s", err->Error());
    } else {
        LOGI("Config loaded from SD Card");
    }
    syncDeviceInfo();

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

    c0Queue.add(timeSync, 5);
    c0Queue.add(checkEthernet, 5);
    c0Queue.add(syncState, 4);

    c1Queue.add(logData, 7);
    c1Queue.add(syncDevices, 6);
    c1Queue.add(deviceActionTask, 5);

    syncState(nullptr);
    ledTimer.attach(1, blinkLED);

    // Indicate that setup is complete and core 1 can start.
    rp2040.fifo.push(1);
}

void loop() {
    server.handleClient();
    handleButtonPress();

    if (!c0Queue.runNextTask()) {
        delay(10);
    }
}

void setup1() {
    // Wait for core 0 to indicate that setup is complete before starting.
    rp2040.fifo.pop();

    // Set the initial monotonic ts so we know
    // how long it took before the first log write.
    initLogData();

    rp2040.wdt_begin(800);
}

void loop1() {
    const unsigned long start = millis();

    collect();

    // Run any available tasks until collection is ready.
    while (millis() - start < 1000) {
        if (!c1Queue.runNextTask()) {
            delay(10);
        }

        rp2040.wdt_reset();
    }
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
        c0Queue.add(addDeviceFromButton, 6);
    }
}

void waitForSerial() {
    if constexpr (WAIT_FOR_SERIAL) {
        while (!Serial) { delay(100); }
    }
}
