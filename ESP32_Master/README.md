# ESP32 Master Node - DuinoCoin ESP-NOW Mining Controller

A master mining controller for DuinoCoin that distributes jobs to slave nodes via ESP-NOW protocol and aggregates results.

## Overview

This sketch turns an ESP32 into a master mining controller that:
- Connects to WiFi and DuinoCoin mining pool
- Fetches mining jobs from the pool
- Distributes jobs to slave ESP32 nodes via ESP-NOW
- Tracks slave statistics and aggregates hashrates
- Automatically refreshes jobs when all are completed

## Features

- **Multi-Slave Support**: Manages up to 10 slave nodes
- **Job Pooling**: Fetches 10 jobs at a time for efficient distribution
- **Round-Robin Distribution**: Assigns unique jobs to each slave
- **Auto-Refresh**: Automatically fetches new jobs when all are completed
- **Statistics Tracking**: Monitors shares, hashrates, and acceptance rates
- **Slave Monitoring**: Detects and marks inactive slaves

## Hardware Requirements

- ESP32 development board
- LED on GPIO 2 (built-in on most boards)
- WiFi network with internet access

## Configuration

Edit `Settings.h` to configure the master node:

### Required Settings

| Setting | Description | Example |
|---------|-------------|---------|
| `DUCO_USER` | Your DuinoCoin username | `"your_username"` |
| `SSID` | WiFi network name | `"your_wifi"` |
| `PASSWORD` | WiFi password | `"your_pass"` |

### Optional Settings

| Setting | Description | Default |
|---------|-------------|---------|
| `MINER_KEY` | Mining key (if enabled) | `"None"` |
| `ESPNOW_CHANNEL` | WiFi channel for ESP-NOW | `1` |
| `MAX_SLAVES` | Maximum number of slaves | `10` |
| `MAX_JOBS` | Jobs to fetch per refresh | `10` |
| `SYNC_INTERVAL` | Status print interval | `30000` ms |
| `SLAVE_TIMEOUT_MS` | Slave inactivity timeout | `30000` ms |

## How It Works

### Startup Sequence

1. Connect to WiFi network
2. Connect to DuinoCoin mining pool
3. Initialize ESP-NOW for slave communication
4. Fetch initial batch of 10 mining jobs
5. Distribute jobs to connected slaves

### Main Loop

1. **Mining**: Master optionally mines on its own core
2. **Monitoring**: Check for inactive slaves every 5 seconds
3. **Auto-Refresh**: When all jobs are completed:
   - Fetch 10 new jobs from the pool
   - Redistribute to all active slaves
4. **Status Report**: Print detailed statistics every 30 seconds

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
| `slaveId` | uint8_t | Reporting slave's ID |
| `counter` | uint32_t | Found counter value |
| `hashrate` | float | Slave's calculated hashrate |
| `elapsedTime` | float | Time to find share |
| `isAccepted` | bool | Share acceptance status |
| `jobId` | uint32_t | Completed job ID |

## Job Management

### Job States

| State | Description |
|-------|-------------|
| Active | Job fetched, not yet assigned |
| Assigned | Job sent to a slave |
| Completed | Slave found valid share |

### Job Flow

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  Fetch Jobs  │───▶│  Distribute  │───▶│   Receive    │
│  from Pool   │    │  to Slaves   │    │   Results    │
└──────────────┘    └──────────────┘    └──────┬───────┘
        ▲                                      │
        │                                      │
        │           ┌──────────────┐           │
        └───────────│  All Jobs    │◀──────────┘
                    │  Completed?  │
                    └──────────────┘
```

## Slave Management

### Slave Information Tracked

| Field | Description |
|-------|-------------|
| `mac` | Slave's MAC address |
| `active` | Whether slave is responsive |
| `lastSeen` | Last communication timestamp |
| `shares` | Total shares submitted |
| `acceptedShares` | Accepted shares count |
| `hashrate` | Average hashrate |
| `currentJobId` | Currently assigned job |

### Auto-Discovery

Slaves are added automatically when they first communicate with the master. The master tracks up to 10 slaves simultaneously.

## Serial Monitor Output

At 115200 baud, the master provides detailed status reports:

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

## LED Indicators

| Pattern | Meaning |
|---------|---------|
| 2 blinks on startup | Setup complete |
| 5 blinks | Client connected |

## Adding Slaves

1. Get the master's MAC address (printed at startup)
2. Configure each slave's `Settings.h` with the master MAC
3. Ensure `ESPNOW_CHANNEL` matches on all devices
4. Power on slaves - they auto-register with master

## Troubleshooting

| Issue | Solution |
|-------|----------|
| WiFi connection failed | Verify SSID and password |
| No slaves connecting | Check MAC address and channel |
| Jobs not refreshing | Check pool connectivity |
| Low acceptance rate | Network latency or pool issues |
| Slaves showing inactive | Check slave power and range |

## Files

| File | Description |
|------|-------------|
| `ESP32_Master.ino` | Main master controller code |
| `Settings.h` | Configuration settings |
| `MiningJob.h` | Mining job handling |
| `DSHA1.h` | SHA1 implementation |
| `Counter.h` | Counter utility class |

## Dependencies

- Arduino ESP32 Core
- ArduinoJson library
- Built-in libraries:
  - `WiFi.h`
  - `esp_now.h`
  - `esp_wifi.h`
  - `esp_task_wdt.h`

## Network Architecture

```
                    ┌─────────────────┐
                    │  DuinoCoin     │
                    │  Mining Pool    │
                    └────────┬────────┘
                             │ WiFi/Internet
                    ┌────────┴────────┐
                    │   ESP32 Master  │
                    │   (This Node)   │
                    └────────┬────────┘
                             │ ESP-NOW
        ┌────────────────────┼────────────────────┐
        │                    │                    │
   ┌────┴────┐         ┌────┴────┐         ┌────┴────┐
   │ Slave 1 │         │ Slave 2 │         │ Slave 3 │
   └─────────┘         └─────────┘         └─────────┘
```

## Performance Tips

- Place master and slaves within 20m for reliable ESP-NOW
- Use the same WiFi channel for all devices
- Ensure adequate power supply for each ESP32
- Monitor serial output for connection issues
