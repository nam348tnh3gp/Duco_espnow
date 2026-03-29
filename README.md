# DuinoCoin ESP-NOW Mining System

A distributed cryptocurrency mining system for DuinoCoin using ESP32 microcontrollers with ESP-NOW protocol for low-latency, WiFi-free communication between nodes.

## Overview

This project implements a master-slave mining architecture where:
- **Master Node**: Connects to the DuinoCoin pool, fetches jobs, and distributes them to slaves
- **Slave Nodes**: Receive jobs via ESP-NOW, compute SHA1 hashes, and report results
- **AP Configuration**: Optional captive portal for easy WiFi setup

```
                    ┌─────────────────┐
                    │  DuinoCoin     │
                    │  Mining Pool   │
                    └────────┬───────┘
                             │ WiFi/Internet
                    ┌────────┴────────┐
                    │   ESP32 Master  │◄──── AP.ino (Optional WiFi Config)
                    └────────┬────────┘
                             │ ESP-NOW (No WiFi needed)
        ┌────────────────────┼────────────────────┐
        │                    │                    │
   ┌────┴────┐         ┌────┴────┐         ┌────┴────┐
   │ Slave 1 │         │ Slave 2 │         │  ...    │
   └─────────┘         └─────────┘         └─────────┘
                    (Up to 10 slaves)
```

## Project Structure

```
Duco_espnow/
├── README.md                 # This file
├── ESP32_Master/
│   ├── ESP32_Master.ino      # Master mining controller
│   ├── AP.ino                # WiFi captive portal configuration
│   ├── Settings.h            # Master configuration
│   ├── MiningJob.h           # Job handling
│   ├── DSHA1.h               # SHA1 implementation
│   ├── Counter.h             # Counter utility
│   ├── README.md             # Master documentation
│   └── AP_README.md          # AP documentation
└── ESP32_Slave/
    ├── ESP32_Slave.ino       # Slave mining node
    ├── Settings.h            # Slave configuration
    ├── MiningJob.h           # Job structure
    ├── DSHA1.h               # SHA1 implementation
    ├── Counter.h             # Counter utility
    └── README.md             # Slave documentation
```

## Components

### 1. ESP32 Master (`ESP32_Master.ino`)

The central controller that:
- Connects to WiFi and DuinoCoin mining pool
- Fetches batches of 10 mining jobs
- Distributes unique jobs to each slave via ESP-NOW
- Tracks slave statistics (hashrate, shares, acceptance rate)
- Auto-refreshes jobs when all are completed
- Monitors slave activity and marks inactive nodes

**Key Features:**
- Supports up to 10 slave nodes
- Round-robin job distribution (no duplicates)
- Real-time statistics reporting every 30 seconds
- Automatic slave discovery

### 2. ESP32 Slave (`ESP32_Slave.ino`)

Mining worker nodes that:
- Connect to master via ESP-NOW (no WiFi required)
- Receive mining jobs with hash targets
- Perform SHA1 computations to find valid shares
- Report results (counter, hashrate, time) back to master

**Key Features:**
- Optimized SHA1 implementation
- LED feedback for share discovery
- Average hashrate tracking
- ~40-80 kH/s per ESP32

### 3. AP Configuration (`AP.ino`)

Optional WiFi configuration portal:
- Creates Access Point `ESP32_PRO` (password: `12345678`)
- Captive portal at `192.168.4.1`
- Web interface for WiFi credential management
- Network scanner, reset, and reboot functions

## Quick Start

### Step 1: Configure Master

1. Open `ESP32_Master/Settings.h`
2. Set your credentials:

```cpp
extern char *DUCO_USER = "your_duinocoin_username";
extern const char SSID[] = "your_wifi_name";
extern const char PASSWORD[] = "your_wifi_password";
```

3. Upload `ESP32_Master.ino` to the master ESP32
4. Note the MAC address printed on Serial Monitor

### Step 2: Configure Slaves

1. Open `ESP32_Slave/Settings.h`
2. Set the master's MAC address and unique slave ID:

```cpp
#define MASTER_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define SLAVE_ID 1  // Unique ID: 1-10
```

3. Upload `ESP32_Slave.ino` to each slave ESP32
4. Change `SLAVE_ID` for each slave (1, 2, 3, etc.)

### Step 3: Power On

1. Power on the master first
2. Wait for "Master ready" message
3. Power on slaves - they auto-register with master

## Configuration Reference

### Master Settings (`ESP32_Master/Settings.h`)

| Setting | Description | Default |
|---------|-------------|---------|
| `DUCO_USER` | DuinoCoin username | Required |
| `SSID` | WiFi network name | Required |
| `PASSWORD` | WiFi password | Required |
| `MINER_KEY` | Mining key (if set) | `"None"` |
| `ESPNOW_CHANNEL` | ESP-NOW channel | `1` |
| `MAX_SLAVES` | Maximum slaves | `10` |
| `MAX_JOBS` | Jobs per batch | `10` |

### Slave Settings (`ESP32_Slave/Settings.h`)

| Setting | Description | Default |
|---------|-------------|---------|
| `MASTER_MAC` | Master's MAC address | Required |
| `SLAVE_ID` | Unique slave ID (1-10) | Required |
| `ESPNOW_CHANNEL` | Must match master | `1` |

## ESP-NOW Communication

ESP-NOW provides direct device-to-device communication:
- **No router needed** between master and slaves
- **Low latency** (~1-2ms)
- **Range**: ~20-30 meters indoors
- **Channel**: All devices must use same WiFi channel

### Message Flow

```
Master                                    Slave
   │                                        │
   │──── Job (hash, difficulty, jobId) ────▶│
   │                                        │
   │                                   [Mining...]
   │                                        │
   │◀── Result (counter, hashrate, time) ───│
   │                                        │
```

## Monitoring

### Serial Output (Master)

The master prints detailed statistics every 30 seconds:

```
╔═══════════════════════════════════════════════════════════╗
║                    Slave Status Report                     ║
╚═══════════════════════════════════════════════════════════╝
Total slaves: 3
Total shares: 150
Total accepted: 148
Accept rate: 98.67%
Total hashrate: 135.50 kH/s

┌─────┬──────────────┬──────────────┬──────────┬──────────┐
│ ID  │  Hashrate    │   Shares     │ Accepted │   Job    │
├─────┼──────────────┼──────────────┼──────────┼──────────┤
│   1 │   45.23 kH/s │          50 │       49 │       42 │
│   2 │   44.80 kH/s │          52 │       51 │       43 │
│   3 │   45.47 kH/s │          48 │       48 │       44 │
└─────┴──────────────┴──────────────┴──────────┴──────────┘
```

### Serial Output (Slave)

```
DuinoCoin ESP-NOW Slave Node 1 v4.3_ESPNOW_SLAVE
ESP-NOW initialized
Slave ready, waiting for jobs...
Slave 1 received job #42, diff=75000
Slave 1 found share: counter=12345, hr=45.23 kH/s, time=0.27s
```

## Hardware Requirements

- **ESP32** development boards (any variant)
- USB cables for programming
- Power supply (USB or battery)

### Recommended Setup

| Role | Quantity | Notes |
|------|----------|-------|
| Master | 1 | Needs WiFi access |
| Slaves | 1-10 | No WiFi needed |

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Master can't connect to WiFi | Verify SSID/password in Settings.h |
| Slaves not appearing | Check MASTER_MAC and ESPNOW_CHANNEL match |
| Low hashrate | Normal: ~40-80 kH/s per ESP32 |
| Jobs not refreshing | Check pool connectivity |
| Slaves going inactive | Check power and range (<30m) |
| "Send failed" errors | Reduce distance or check channel |

## Getting MAC Address

Upload this sketch to find an ESP32's MAC address:

```cpp
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
```

## Dependencies

- **Arduino IDE** with ESP32 board support
- **ArduinoJson** library (for master)
- Built-in ESP32 libraries (WiFi, esp_now)

### Installing ESP32 Board Support

1. Open Arduino IDE → Preferences
2. Add to "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Tools → Board Manager → Search "ESP32" → Install

## Performance

| Metric | Typical Value |
|--------|---------------|
| Hashrate per slave | 40-80 kH/s |
| Share time | 0.1-2.0 seconds |
| Acceptance rate | >95% |
| ESP-NOW latency | 1-2 ms |

## License

This project is for educational purposes. DuinoCoin mining is subject to the [DuinoCoin terms](https://duinocoin.com).

## Links

- [DuinoCoin Website](https://duinocoin.com)
- [DuinoCoin Wallet](https://wallet.duinocoin.com)
- [ESP-NOW Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
