# Home Monitoring Network — Environmental Sensor Node

A Bluetooth Low Energy (BLE) environmental monitoring node built around the **Particle P2 (Photon 2)** and **Honeywell HIH-6130 temperature and humidity sensor**.

This project is part of the Home Monitoring Network, a distributed environmental monitoring system designed to collect measurements from multiple locations and transmit them wirelessly to a central gateway.

The environmental sensor node periodically measures ambient temperature and relative humidity, then transmits the measurements to a BLE-connected gateway.

## Overview

The environmental sensor node provides:

- Temperature measurements in Celsius and Fahrenheit.
- Relative humidity measurements (%RH).
- HIH-6130 sensor communication over I²C.
- BLE peripheral functionality.
- Custom BLE GATT service and characteristic.
- BLE notifications containing environmental measurements.
- Automatic BLE advertising recovery after disconnection.
- Independent sensor operation when the gateway is unavailable.
- Serial diagnostics for measurements and BLE activity.
- A measurement sequence counter for monitoring communication reliability.

The firmware operates independently of the Particle Cloud using `SYSTEM_MODE(MANUAL)`.

## Hardware

### Components

| Component | Description |
|---|---|
| Microcontroller | Particle P2 / Photon 2 |
| Expansion board | Adafruit FeatherWing Doubler |
| Environmental sensor | Honeywell HIH-6130 |
| Wireless communication | Bluetooth Low Energy |
| Sensor interface | I²C |
| Operating voltage | 3.3 V |

### I²C Configuration

The HIH-6130 communicates with the Particle P2 over I²C.

| Signal | Particle P2 |
|---|---|
| SDA | D0 |
| SCL | D1 |
| VCC | 3V3 |
| GND | GND |

**I²C Address:** `0x27`

**I²C Clock:** 100 kHz

The firmware initializes the I²C interface using:

```cpp
Wire.begin();
Wire.setSpeed(CLOCK_SPEED_100KHZ);
```

Ensure that appropriate I²C pull-up resistors are present on the SDA and SCL lines.

## Software Requirements

| Component | Version |
|---|---|
| Particle Device OS | 6.4.1 |
| Particle Workbench | Compatible version |
| Development environment | Visual Studio Code |
| Target platform | P2 / Photon 2 |

The application uses the Particle Device OS APIs and does not require additional third-party firmware libraries.

### Device OS Configuration

```cpp
#include "Particle.h"

SYSTEM_MODE(MANUAL);
```

The application does not require a Particle Cloud connection for normal operation.

## Sensor Operation

The Honeywell HIH-6130 measures ambient temperature and relative humidity.

Measurements are collected every **30 seconds**.

### Measurement Sequence

1. Initiate a measurement through I²C.
2. Wait 50 milliseconds for measurement completion.
3. Read four bytes from the HIH-6130.
4. Validate the sensor status bits.
5. Convert raw sensor values into temperature and relative humidity.
6. Increment the measurement sequence counter.
7. Encode the measurement into an eight-byte BLE payload.
8. Transmit the measurement when a BLE central is connected.

Measurements continue even when the BLE gateway is unavailable.

### Temperature Conversion

The firmware calculates temperature in Celsius using:

```cpp
temperature =
    (rawTemperature * 165.0f) / 16382.0f - 40.0f;
```

Temperature in Fahrenheit is calculated for serial diagnostic output:

```cpp
temperatureF =
    temperature * 9.0f / 5.0f + 32.0f;
```

### Relative Humidity Conversion

```cpp
humidity =
    (rawHumidity * 100.0f) / 16382.0f;
```

## Bluetooth Low Energy

The Particle P2 operates as a **BLE peripheral**.

The gateway operates as the BLE central and connects to the environmental sensor node.

### Device Identity

**BLE Device Name:**

`HM-Garage-ENV`

The device name is currently configured for the garage environmental sensor.

### BLE Service

The application implements a custom GATT service.

| Property | Value |
|---|---|
| Service UUID | `8b7e0001-6a2d-4c91-9e31-4f2a8c6d1000` |
| Characteristic UUID | `8b7e0002-6a2d-4c91-9e31-4f2a8c6d1000` |
| Characteristic name | Environmental Data |
| Characteristic properties | READ, NOTIFY |
| Payload size | 8 bytes |

The characteristic supports:

**READ:** Allows a connected BLE central to retrieve the current characteristic value.

**NOTIFY:** Allows a subscribed BLE central to receive environmental measurement updates.

### BLE Advertising

The node advertises the custom service UUID in its primary advertising packet.

The device name is included in the scan-response data.

This arrangement allows the gateway to discover compatible sensor nodes using the service UUID without depending on the advertised device name.

### Connection Recovery

The firmware periodically checks the BLE connection and advertising state.

When no BLE central is connected and advertising has stopped, the firmware requests that advertising restart.

This allows the gateway to reconnect after events such as:

- Gateway restart.
- Temporary BLE connection loss.
- Sensor node restart.
- Gateway disconnection.

The sensor continues collecting measurements while disconnected.

## Environmental Data Protocol

Environmental measurements are transmitted using a fixed-length, eight-byte binary payload.

All multibyte values use **little-endian byte order**.

### Payload Structure

| Byte Offset | Size | Data Type | Description |
|---|---|---|---|
| 0–1 | 2 bytes | `int16_t` | Temperature in Celsius × 100 |
| 2–3 | 2 bytes | `uint16_t` | Relative humidity × 100 |
| 4–7 | 4 bytes | `uint32_t` | Measurement sequence number |

**Total payload size: 8 bytes**

### Example

Consider the following measurement:

| Parameter | Value |
|---|---|
| Temperature | 22.45 °C |
| Relative humidity | 48.32% |
| Sequence number | 2 |

The encoded values are:

```text
Temperature: 2245  = 0x08C5
Humidity:    4832  = 0x12E0
Sequence:    2     = 0x00000002
```

The transmitted payload is:

```text
C5 08 E0 12 02 00 00 00
```

### Decoding

The receiving gateway reconstructs the values using:

```cpp
int16_t temperatureRaw =
    static_cast<int16_t>(
        static_cast<uint16_t>(payload[0]) |
        (static_cast<uint16_t>(payload[1]) << 8)
    );

uint16_t humidityRaw =
    static_cast<uint16_t>(payload[2]) |
    (static_cast<uint16_t>(payload[3]) << 8);

uint32_t sequence =
    static_cast<uint32_t>(payload[4]) |
    (static_cast<uint32_t>(payload[5]) << 8) |
    (static_cast<uint32_t>(payload[6]) << 16) |
    (static_cast<uint32_t>(payload[7]) << 24);

float temperature = temperatureRaw / 100.0f;
float humidity = humidityRaw / 100.0f;
```

The sequence number can be used by the gateway to identify missing or out-of-order measurements.

The sequence counter resets when the sensor node restarts.

## Firmware Configuration

The following parameters are defined near the beginning of the application.

| Parameter | Default | Description |
|---|---|---|
| `HIH6130_ADDRESS` | `0x27` | Sensor I²C address |
| `REPORT_INTERVAL_MS` | `30000` | Measurement interval |
| `MEASUREMENT_DELAY_MS` | `50` | Sensor conversion delay |
| `BLE_CHECK_INTERVAL_MS` | `3000` | BLE connection monitoring interval |
| `BLE_DEVICE_NAME` | `HM-Garage-ENV` | BLE device name |

### Changing the Measurement Interval

To modify the reporting frequency, change:

```cpp
constexpr uint32_t REPORT_INTERVAL_MS = 30000;
```

For example, to collect measurements every 60 seconds:

```cpp
constexpr uint32_t REPORT_INTERVAL_MS = 60000;
```

The gateway should be configured with a data freshness timeout greater than the reporting interval.

## Building and Flashing

The project is developed using Particle Workbench in Visual Studio Code.

### Prerequisites

1. Install Visual Studio Code.
2. Install the Particle Workbench extension.
3. Configure the project for the P2 platform.
4. Select Particle Device OS 6.4.1.
5. Connect the Particle P2 over USB.

### Build

Use the Particle Workbench command:

**Particle: Compile Application (Local)**

### Flash

Use:

**Particle: Flash Application (Local)**

The device should restart and initialize the HIH-6130 and BLE subsystem.

## Serial Diagnostics

The firmware provides diagnostic output over USB serial.

**Baud Rate:** 115200

### Startup Output

```text
================================
 HOME MONITORING NETWORK
 Environmental Sensor Node
 Particle P2 / HIH-6130
================================

I2C initialized
Sensor address: 0x27
Reporting interval: 30 seconds

Starting BLE advertising...
BLE advertising requested

BLE initialized
GATT characteristic registered
Device name: HM-Garage-ENV
```

### Environmental Measurement

Example output:

```text
Environmental Measurement
-------------------------
Temperature: 22.45 C / 72.41 F
Humidity:    48.32 %RH
Uptime:      62 seconds

BLE characteristic updated
Payload: C5 08 E0 12 02 00 00 00
Sequence: 2
```

### Gateway Disconnection

When the BLE gateway disconnects:

```text
BLE gateway disconnected
```

If advertising has stopped, the firmware attempts to restart it.

While disconnected, sensor measurements continue:

```text
BLE offline - measurement 3 not transmitted
```

The node resumes characteristic updates when a gateway reconnects.

## Troubleshooting

### HIH-6130 Not Responding

**Symptoms:**

```text
ERROR: HIH-6130 measurement request failed