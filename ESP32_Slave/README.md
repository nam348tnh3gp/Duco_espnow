# ESP32 Slave Node - DuinoCoin ESP-NOW Miner

A slave mining node for DuinoCoin that receives jobs from a master node via ESP-NOW protocol and performs SHA1 hash computations.

## Overview

This sketch turns an ESP32 into a slave mining node that:
- Connects to a master node via ESP-NOW (no WiFi required)
- Receives mining jobs from the master
- Performs SHA1 hash computation to find valid shares
- Reports results back to the master node

## Hardware Requirements

- ESP32 development board
- LED on GPIO 2 (built-in on most boards)

## Configuration

Edit `Settings.h` to configure the slave node:

### Required Settings

| Setting | Description | Example |
|---------|-------------|---------|
| `MASTER_MAC` | MAC address of the master ESP32 | `{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}` |
| `SLAVE_ID` | Unique ID for this slave (1-10) | `1` |

### Optional Settings

| Setting | Description | Default |
|---------|-------------|---------|
| `ESPNOW_CHANNEL` | WiFi channel for ESP-NOW | `1` |
| `LED_BLINKING` | Enable LED feedback | Enabled |
| `SERIAL_PRINTING` | Enable serial output | Enabled |
| `SERIAL_BAUDRATE` | Serial baud rate | `115200` |

## How It Works

1. **Initialization**: The slave initializes ESP-NOW and registers the master node as a peer
2. **Wait for Job**: The slave waits to receive a mining job from the master
3. **Mining**: When a job is received, the slave:
   - Parses the last block hash and expected hash
   - Iterates through counter values (0 to difficulty)
   - Computes SHA1(lastBlockHash + counter) for each value
   - Finds the counter that produces the expected hash
4. **Report Result**: Sends the result (counter, hashrate, elapsed time) back to master

## ESP-NOW Message Structures

### Job Message (Master → Slave)

| Field | Type | Description |
|-------|------|-------------|
| `msgType` | uint8_t | Always `1` for job messages |
| `jobId` | uint32_t | Unique job identifier |
| `lastBlockHash` | char[65] | Previous block hash |
| `expectedHash` | char[41] | Target hash to find |
| `difficulty` | uint32_t | Maximum counter value |

### Result Message (Slave → Master)

| Field | Type | Description |
|-------|------|-------------|
| `msgType` | uint8_t | Always `0` for result messages |
| `slaveId` | uint8_t | This slave's ID |
| `counter` | uint32_t | Found counter value |
| `hashrate` | float | Calculated hashrate (H/s) |
| `elapsedTime` | float | Time to find share (seconds) |
| `isAccepted` | bool | Share acceptance status |
| `jobId` | uint32_t | Job ID being reported |

## LED Indicators

| Pattern | Meaning |
|---------|---------|
| 2 blinks on startup | Setup complete |
| Single flash | Share found |

## Serial Monitor Output

At 115200 baud, you'll see:
- Startup information
- Job received notifications
- Share found details (counter, hashrate, time)
- Send status confirmations

Example output:
```
DuinoCoin ESP-NOW Slave Node 1 v4.3_ESPNOW_SLAVE
ESP-NOW initialized
Slave ready, waiting for jobs...
Slave 1 received job #42, diff=75000
Slave 1 found share: counter=12345, hr=45.23 kH/s, time=0.27s, avg=44.80 kH/s
Slave 1: Result sent - counter=12345, hr=45.23
```

## Getting the Master MAC Address

To find your master ESP32's MAC address, upload this sketch to the master:

```cpp
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
```

Then update `MASTER_MAC` in the slave's `Settings.h`:
```cpp
#define MASTER_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Not receiving jobs | Verify `MASTER_MAC` and `ESPNOW_CHANNEL` match master |
| Send failures | Check master is powered on and in range |
| Low hashrate | Normal for ESP32 (~40-80 kH/s per core) |
| Watchdog reset | Reduce `WATCHDOG_TIMEOUT` if needed |

## Files

| File | Description |
|------|-------------|
| `ESP32_Slave.ino` | Main slave code |
| `Settings.h` | Configuration settings |
| `MiningJob.h` | Mining job structure |
| `DSHA1.h` | Optimized SHA1 implementation |
| `Counter.h` | Counter utility class |

## Dependencies

- Arduino ESP32 Core
- Built-in ESP-NOW library (`esp_now.h`)
- Built-in WiFi library (`WiFi.h`)
