# ESP32 WiFi Router PRO - Captive Portal Configuration

A WiFi Access Point with captive portal for configuring ESP32 network settings via a web interface.

## Overview

This sketch creates a configurable WiFi router/repeater with:
- Dual-mode operation (Access Point + Station)
- Captive portal for easy configuration
- Web-based settings interface
- mDNS support for easy access
- Persistent settings storage

## Features

- **Access Point**: Creates a WiFi network for devices to connect
- **Station Mode**: Connects to an upstream WiFi network
- **Captive Portal**: Redirects all DNS queries to the configuration page
- **Web Interface**: Beautiful mobile-friendly UI for configuration
- **WiFi Scanner**: Scan and select available networks
- **Persistent Storage**: Settings survive reboot
- **Internet Check**: Monitors upstream connectivity

## Hardware Requirements

- ESP32 development board

## Default Settings

| Setting | Value |
|---------|-------|
| AP SSID | `ESP32_PRO` |
| AP Password | `12345678` |
| AP IP Address | `192.168.4.1` |
| mDNS Hostname | `esp32.local` |

## Getting Started

1. Upload the sketch to your ESP32
2. Connect to the `ESP32_PRO` WiFi network (password: `12345678`)
3. A captive portal should open automatically
4. If not, navigate to `http://192.168.4.1` or `http://esp32.local`

## Web Interface

### Main Dashboard (`/`)

The dashboard displays:
- **WiFi Status**: Connected/Disconnected to upstream network
- **Internet Status**: OK/No connectivity
- **AP Name**: Current access point SSID
- **STA Name**: Currently configured station SSID
- **Connected Clients**: Number of devices connected to AP
- **Uptime**: Time since last boot

### Available Actions

| Button | Description |
|--------|-------------|
| **Save** (WiFi) | Save station WiFi credentials |
| **Save** (AP) | Change access point name/password |
| **Scan WiFi** | Scan for available networks |
| **Reset** | Clear all settings and reboot |
| **Reboot** | Restart the ESP32 |

## API Endpoints

### Web Pages

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main configuration dashboard |
| `/scan` | GET | WiFi network scanner page |

### Actions

| Endpoint | Method | Parameters | Description |
|----------|--------|------------|-------------|
| `/save-sta` | POST | `ssid`, `pass` | Save station WiFi credentials |
| `/save-ap` | POST | `ssid`, `pass` | Change AP SSID and password |
| `/reset` | GET | - | Clear all settings and reboot |
| `/reboot` | GET | - | Restart the device |

### Status API

| Endpoint | Method | Response |
|----------|--------|----------|
| `/status` | GET | JSON status object |

**Status Response Example:**
```json
{
  "wifi": "connected",
  "internet": true
}
```

## WiFi States

The device manages WiFi connection with these states:

| State | Description |
|-------|-------------|
| `WIFI_IDLE` | No connection attempt |
| `WIFI_CONNECTING` | Attempting to connect (15s timeout) |
| `WIFI_CONNECTED` | Successfully connected |
| `WIFI_FAILED` | Connection failed (retry after 10s) |

## Captive Portal

The captive portal works by:
1. Running a DNS server that resolves all domains to `192.168.4.1`
2. Redirecting all HTTP requests to the configuration page
3. Automatically triggering on most devices when connecting

## Storage

Settings are stored in ESP32's Preferences (NVS):

| Key | Description |
|-----|-------------|
| `sta_ssid` | Station WiFi SSID |
| `sta_pass` | Station WiFi password |
| `ap_ssid` | Access Point SSID |
| `ap_pass` | Access Point password |

## Internet Connectivity Check

The device checks internet connectivity by:
- Making HTTP requests to `http://clients3.google.com/generate_204`
- Expecting a 204 response code
- Checking every 30 seconds when connected

## Network Architecture

```
                    ┌─────────────────┐
                    │  Internet/      │
                    │  Router         │
                    └────────┬────────┘
                             │ STA Mode
                    ┌────────┴────────┐
                    │   ESP32_PRO     │
                    │  192.168.4.1    │
                    └────────┬────────┘
                             │ AP Mode
        ┌────────────────────┼────────────────────┐
        │                    │                    │
   ┌────┴────┐         ┌────┴────┐         ┌────┴────┐
   │ Phone   │         │ Laptop  │         │ Tablet  │
   └─────────┘         └─────────┘         └─────────┘
```

## Serial Monitor

At 115200 baud, you'll see:
- mDNS status: `mDNS: http://esp32.local`
- WiFi connection events

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Can't connect to AP | Verify password is at least 8 characters |
| Captive portal not appearing | Navigate manually to `192.168.4.1` |
| Internet check failing | Verify upstream WiFi credentials |
| Settings not saving | Check NVS partition is not corrupted |
| mDNS not working | Use IP address `192.168.4.1` instead |

## Dependencies

- Arduino ESP32 Core
- Built-in libraries:
  - `WiFi.h`
  - `DNSServer.h`
  - `WebServer.h`
  - `ESPmDNS.h`
  - `Preferences.h`
  - `HTTPClient.h`
