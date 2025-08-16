#include "auramon.h"

Wiznet5500lwIP eth(PIN_SPI0_SS, SPI, ETH_INT);

mutex_t sdMu;
SdFs sd;

ModbusRTUMaster modbus(Serial1, RS485_DE);

inputDevice * *devices = new inputDevice *[MAX_DEVICES];

extern void waitForSerial();

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);
    waitForSerial();

    Serial.println("Booting");

    // Start the Ethernet port.
    // eth.setSPISpeed(33);
    eth.hostname("aura-mon");
    if (!eth.begin(mac)) {
        Serial.println("No wired Ethernet hardware detected. Check pinouts, wiring.");
        while (1) { delay(1000); }
    }

    // Wait until IP was acquired.
    while (!eth.connected()) {
        // Serial.println("Waiting for DHCP address..");
        delay(500);
    }

    Serial.println("Ethernet Ready!");
    Serial.print("IP address: ");
    Serial.println(eth.localIP());
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);

    delay(1000);

    digitalWrite(LED_BUILTIN, LOW);

    delay(500);
}

float uint16ToFloat(uint16_t words[2]) {
    float f;
    memcpy(&f, words, sizeof(float));
    return f;
}

void setup1() {
    waitForSerial();

    // mutex_init(&sdMu);
    // if (!sd.begin(SD_CONFIG)) {
    //     Serial.println("Failed to initialize SD Card");
    //     sd.initErrorPrint(&Serial);
    //     while (1) { delay(1000); }
    // }
    //
    // Serial.println("Initialized SD Card!");
    //
    // mutex_enter_timeout_ms(&sdMu, 500);
    // FsFile fTest = sd.open("/test.txt");
    // String str = fTest.readString();
    // fTest.close();
    // mutex_exit(&sdMu);
    //
    // Serial.println("Got data from file: " + str);

    Serial1.begin(RS485_BAUDRATE);
    modbus.begin(RS485_BAUDRATE);
    modbus.setTimeout(1000);

    // if (const uint8_t err = modbus.writeSingleHoldingRegister(247, 0x2710, 0x5055)) {
    //     Serial.print("Could not set locating: ");
    //     Serial.println(modbusError(err));
    // }

    // assignModbusAddress(2);

    // TODO: temp until I have config.
    devices[0] = new inputDevice(0);
    devices[1] = new inputDevice(1);

    Serial.println("Modbus initialised");
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
