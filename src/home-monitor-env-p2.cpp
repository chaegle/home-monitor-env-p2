/*
 * Project: Home Monitoring Network - Environmental Sensor Node
 * Author: Cameron Haegle
 * Date: 10/24/2026
 *
 * Hardware:
 *   Particle P2 / Photon 2
 *   Adafruit FeatherWing Doubler
 *   Honeywell HIH-6130 Temperature/Humidity Sensor
 *
 * Device OS: 6.4.1
 */

#include "Particle.h"

SYSTEM_MODE(MANUAL);

// ============================================================
// Configuration
// ============================================================

constexpr uint8_t HIH6130_ADDRESS = 0x27;

constexpr uint32_t REPORT_INTERVAL_MS = 30000;
constexpr uint32_t MEASUREMENT_DELAY_MS = 50;
constexpr uint32_t BLE_CHECK_INTERVAL_MS = 3000;

const char BLE_DEVICE_NAME[] = "HM-Garage-ENV";

// ============================================================
// BLE UUIDs
// ============================================================

BleUuid homeMonitoringService(
    "8b7e0001-6a2d-4c91-9e31-4f2a8c6d1000"
);

BleUuid environmentalDataUuid(
    "8b7e0002-6a2d-4c91-9e31-4f2a8c6d1000"
);

// ============================================================
// BLE Characteristic
// ============================================================

BleCharacteristic environmentalDataCharacteristic(
    "Environmental Data",
    static_cast<BleCharacteristicProperty>(
        static_cast<uint8_t>(BleCharacteristicProperty::READ) |
        static_cast<uint8_t>(BleCharacteristicProperty::NOTIFY)
    ),
    environmentalDataUuid,
    homeMonitoringService
);

// ============================================================
// BLE Advertising Data
// ============================================================

// Keep advertising configuration available for recovery.

BleAdvertisingData advertisingData;
BleAdvertisingData scanResponse;

// ============================================================
// Global Variables
// ============================================================

uint32_t lastReport = 0;
uint32_t lastBLECheck = 0;

uint32_t measurementSequence = 0;

bool previousConnected = false;

// ============================================================
// HIH-6130 Measurement
// ============================================================

bool readHIH6130(float &temperature, float &humidity)
{
    Wire.beginTransmission(HIH6130_ADDRESS);

    uint8_t result = Wire.endTransmission();

    if (result != 0) {
        Serial.printf(
            "ERROR: HIH-6130 measurement request failed: %u\n",
            result
        );
        return false;
    }

    delay(MEASUREMENT_DELAY_MS);

    if (Wire.requestFrom(HIH6130_ADDRESS, (uint8_t)4) != 4) {
        Serial.println("ERROR: Unable to read HIH-6130");
        return false;
    }

    uint8_t data[4];

    for (int i = 0; i < 4; i++) {
        data[i] = Wire.read();
    }

    uint8_t status = (data[0] >> 6) & 0x03;

    if (status != 0) {
        Serial.printf(
            "ERROR: HIH-6130 status = %u\n",
            status
        );
        return false;
    }

    uint16_t rawHumidity =
        ((data[0] & 0x3F) << 8) | data[1];

    uint16_t rawTemperature =
        (data[2] << 6) | (data[3] >> 2);

    humidity =
        (rawHumidity * 100.0f) / 16382.0f;

    temperature =
        (rawTemperature * 165.0f) / 16382.0f - 40.0f;

    return true;
}

// ============================================================
// Encode Environmental Data
// ============================================================

void encodeEnvironmentalData(
    float temperature,
    float humidity,
    uint32_t sequence,
    uint8_t payload[8]
)
{
    int16_t tempEncoded =
        static_cast<int16_t>(temperature * 100.0f);

    uint16_t humidityEncoded =
        static_cast<uint16_t>(humidity * 100.0f);

    uint16_t tempBits =
        static_cast<uint16_t>(tempEncoded);

    payload[0] = tempBits & 0xFF;
    payload[1] = (tempBits >> 8) & 0xFF;

    payload[2] = humidityEncoded & 0xFF;
    payload[3] = (humidityEncoded >> 8) & 0xFF;

    payload[4] = sequence & 0xFF;
    payload[5] = (sequence >> 8) & 0xFF;
    payload[6] = (sequence >> 16) & 0xFF;
    payload[7] = (sequence >> 24) & 0xFF;
}

// ============================================================
// BLE Advertising
// ============================================================

void startAdvertising()
{
    Serial.println("Starting BLE advertising...");

    BLE.advertise(
        &advertisingData,
        &scanResponse
    );

    Serial.println("BLE advertising requested");
}

// ============================================================
// BLE Connection Monitoring
// ============================================================

void monitorBLE()
{
    bool connected = BLE.connected();

    // Report connection-state changes.

    if (connected != previousConnected) {

        if (connected) {
            Serial.println("BLE gateway connected");
        }
        else {
            Serial.println("BLE gateway disconnected");
        }

        previousConnected = connected;
    }

    // Restart advertising if no central is connected
    // and the device is not currently advertising.

    if (!connected && !BLE.advertising()) {

        Serial.println(
            "BLE disconnected and advertising stopped"
        );

        startAdvertising();
    }
}

// ============================================================
// Publish Environmental Measurement
// ============================================================

void publishEnvironmentalData(
    float temperature,
    float humidity
)
{
    uint8_t payload[8];

    measurementSequence++;

    encodeEnvironmentalData(
        temperature,
        humidity,
        measurementSequence,
        payload
    );

    // The HIH-6130 continues to be measured even
    // when the gateway is disconnected.

    if (!BLE.connected()) {

        Serial.printf(
            "BLE offline - measurement %lu not transmitted\n",
            (unsigned long)measurementSequence
        );

        return;
    }

    ssize_t result =
        environmentalDataCharacteristic.setValue(
            payload,
            sizeof(payload)
        );

    if (result < 0) {

        Serial.printf(
            "ERROR: BLE characteristic update failed: %d\n",
            (int)result
        );

        return;
    }

    if (result != sizeof(payload)) {

        Serial.printf(
            "ERROR: Incomplete BLE characteristic update: %d bytes\n",
            (int)result
        );

        return;
    }

    Serial.println("BLE characteristic updated");

    Serial.print("Payload: ");

    for (size_t i = 0; i < sizeof(payload); i++) {
        Serial.printf("%02X ", payload[i]);
    }

    Serial.println();

    Serial.printf(
        "Sequence: %lu\n",
        (unsigned long)measurementSequence
    );
}

// ============================================================
// BLE Initialization
// ============================================================

void initializeBLE()
{
    BLE.on();

    BLE.addCharacteristic(
        environmentalDataCharacteristic
    );

    advertisingData.appendServiceUUID(
        homeMonitoringService
    );

    scanResponse.appendLocalName(
        BLE_DEVICE_NAME
    );

    startAdvertising();

    previousConnected = BLE.connected();

    Serial.println();
    Serial.println("BLE initialized");
    Serial.println("GATT characteristic registered");

    Serial.printf(
        "Device name: %s\n",
        BLE_DEVICE_NAME
    );
}

// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);

    waitFor(Serial.isConnected, 10000);

    Serial.println();
    Serial.println("================================");
    Serial.println(" HOME MONITORING NETWORK");
    Serial.println(" Environmental Sensor Node");
    Serial.println(" Particle P2 / HIH-6130");
    Serial.println("================================");

    Wire.begin();
    Wire.setSpeed(CLOCK_SPEED_100KHZ);

    Serial.println("I2C initialized");
    Serial.println("Sensor address: 0x27");
    Serial.println("Reporting interval: 30 seconds");

    initializeBLE();

    lastReport = millis() - REPORT_INTERVAL_MS;
    lastBLECheck = millis();
}

// ============================================================
// Main Loop
// ============================================================

void loop()
{
    uint32_t now = millis();

    // --------------------------------------------------------
    // Monitor BLE connection and advertising.
    // --------------------------------------------------------

    if (now - lastBLECheck >= BLE_CHECK_INTERVAL_MS) {

        lastBLECheck = now;

        monitorBLE();
    }

    // --------------------------------------------------------
    // Read and publish environmental measurements.
    // --------------------------------------------------------

    if (now - lastReport >= REPORT_INTERVAL_MS) {

        lastReport = now;

        float temperature;
        float humidity;

        if (readHIH6130(temperature, humidity)) {

            float temperatureF =
                temperature * 9.0f / 5.0f + 32.0f;

            Serial.println();
            Serial.println("Environmental Measurement");
            Serial.println("-------------------------");

            Serial.printf(
                "Temperature: %.2f C / %.2f F\n",
                temperature,
                temperatureF
            );

            Serial.printf(
                "Humidity:    %.2f %%RH\n",
                humidity
            );

            Serial.printf(
                "Uptime:      %lu seconds\n",
                millis() / 1000
            );

            publishEnvironmentalData(
                temperature,
                humidity
            );

        }
        else {

            Serial.println(
                "Failed to retrieve environmental data"
            );
        }
    }
}