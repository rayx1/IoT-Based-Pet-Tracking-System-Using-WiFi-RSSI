# iot-pet-tracker-espnow-rssi

IoT pet tracker built with NodeMCU ESP8266 boards, ESP-NOW, RSSI-based proximity detection, buzzer alerts, SMTP email warnings, and a built-in web dashboard. Supports **multiple pet nodes** tracked from a single home node.

## Features

- One Home Node supervises **N pet nodes** (one collar per pet)
- Pet Node sends validated ESP-NOW packets with pet ID, packet counter, uptime, and battery voltage field
- Home Node accepts packets only from the configured pet roster MACs
- RSSI samples are filtered per-pet by source MAC to reduce interference from unrelated WiFi traffic
- Per-pet status: `Nearby`, `Far`, `Lost`, `Waiting`
- Buzzer alert reflects the most urgent pet: slow beep for any `Far`, faster beep when any pet is `Lost`
- Per-pet SMTP email cooldown so each pet alerts independently without spam
- Dashboard renders one card per pet plus a global control panel
- Dashboard `Silence` control is a POST endpoint with optional shared-token guard
- JSON API at `/json` returns the full roster as an array
- Serial debug logs for both nodes
- Placeholder configuration section at the top of each sketch, with optional ignored `secrets.h`
- Safe fallback behavior for WiFi or ESP-NOW setup failures

## System Architecture

```text
                   Home WiFi Router
                 +------------------+
                 |  SSID / Channel  |
                 +---------+--------+
                           |
            locks channel  |  locks channel
              (per pet)    |
      +--------------------+--------------------+
      |                    |                    |
      v                    v                    v
+------------------+ +------------------+ +----------------------+
| Pet Node 1       | | Pet Node 2       | | Home Node            |
| NodeMCU ESP8266  | | NodeMCU ESP8266  | | NodeMCU ESP8266      |
| Collar mounted   | | Collar mounted   | | Buzzer on D5         |
| Sends pet data   | | Sends pet data   | | Dashboard + Email    |
+--------+---------+ +--------+---------+ +----------+-----------+
         | ESP-NOW           | ESP-NOW              |
         +-------------------+--------------------->|
                                                    v
                                          +------------------+
                                          | User Browser     |
                                          | / and SMTP inbox |
                                          +------------------+
```

## Hardware Required

- 1 x NodeMCU ESP8266 for the Home Node
- 1 x NodeMCU ESP8266 **per pet** for Pet Nodes
- 1 x active buzzer (Home Node)
- Jumper wires
- USB cable for each board
- Optional battery pack for each pet node

## Wiring

| Home Node Pin | Connect To | Notes |
| --- | --- | --- |
| `D5` | Active buzzer signal pin | Alert output |
| `GND` | Active buzzer ground pin | Shared ground |
| `VIN` or USB | Power input | Use stable 5V USB or regulated supply |

See full notes in [docs/wiring.md](docs/wiring.md).

## Software / Libraries Required

- Arduino IDE 2.x or newer
- ESP8266 Arduino core
- Built-in ESP8266 libraries:
  - `ESP8266WiFi`
  - `ESP8266WebServer`
  - `espnow`
- External library:
  - `ESP Mail Client` by Mobizt

## Repository Layout

```text
iot-pet-tracker-espnow-rssi/
|-- README.md
|-- LICENSE
|-- .gitignore
|-- firmware/
|   |-- pet_node/
|   |   |-- pet_node.ino
|   |   `-- secrets.example.h
|   `-- home_node/
|       |-- home_node.ino
|       `-- secrets.example.h
`-- docs/
    |-- wiring.md
    |-- setup.md
    `-- troubleshooting.md
```

## Installation

1. Install Arduino IDE.
2. Install the ESP8266 board package from Board Manager.
3. Install the `ESP Mail Client` library from Library Manager.
4. **For each pet node**: edit `PET_ID_VALUE` to a unique string (`PET-001`, `PET-002`, ...), upload, copy its MAC from Serial Monitor.
5. Add an entry per pet to the Home Node `PET_NODE_LIST` (MAC, pet ID, display name).
6. Upload the Home Node and copy its MAC from Serial Monitor.
7. Paste the Home Node MAC into each pet node's `HOME_NODE_MAC_BYTES`.
8. Upload each pet node again.
9. Open the Home Node IP shown in Serial Monitor in a browser.

Detailed guide: [docs/setup.md](docs/setup.md).

## Configuring Multiple Pets

The Home Node holds a roster macro `PET_NODE_LIST`. Each entry binds one MAC to a protocol pet ID and a friendly display name:

```cpp
#define PET_NODE_LIST \
  { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}, "PET-001", "Bella" }, \
  { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCD}, "PET-002", "Max"   }, \
  { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCE}, "PET-003", "Coco"  }
```

Rules:

- Every pet node sketch must set `PET_ID_VALUE` to the same string used in the home node entry for that MAC.
- MAC bytes in the home node entry must match the MAC printed by that pet node's Serial Monitor.
- Add entries by appending lines (with trailing `\` continuation). The home node sizes its arrays automatically.
- Each pet has its own status, RSSI, buzzer-influence, and email cooldown.

If a pet node's `PET_ID_VALUE` does not match the entry for its MAC, the home node logs `Rejected packet ... pet ID mismatch`.

## How to Find a Node's MAC Address

1. Upload the sketch.
2. Open Serial Monitor at `115200` baud.
3. Wait for the startup logs.
4. Copy the printed MAC.

Example:

```text
Home Node MAC: 84:F3:EB:12:34:56
```

## Optional secrets.h Workflow

Each firmware folder includes a `secrets.example.h`.

1. Duplicate `secrets.example.h` as `secrets.h`.
2. Edit `secrets.h` with your WiFi, SMTP, MAC, and pet roster values.
3. Keep `secrets.h` private. It is already ignored by `.gitignore`.

## How to Configure Email SMTP

Edit these placeholders at the top of `home_node.ino` (or in `secrets.h`):

- `WIFI_SSID_VALUE`
- `WIFI_PASSWORD_VALUE`
- `SMTP_HOST_VALUE`
- `SMTP_PORT_VALUE`
- `SENDER_EMAIL_VALUE`
- `SENDER_APP_PASSWORD_VALUE`
- `RECIPIENT_EMAIL_VALUE`
- `SMTP_TIME_GMT_OFFSET_VALUE`

Example for Gmail SMTP:

- Host: `smtp.gmail.com`
- Port: `465`
- Sender email: your Gmail address
- App password: 16-character Gmail App Password

### SMTP timezone offset

`SMTP_TIME_GMT_OFFSET_VALUE` controls the DATE header that ESP-Mail-Client writes. The unit depends on the installed library version:

- Older builds expect **seconds** (`19800` = UTC+5:30 IST)
- Newer builds expect **hours** (`5.5` = UTC+5:30 IST)

If unsure, leave it at `0`. SMTP servers usually rewrite the timestamp anyway.

### Gmail App Password Note

For Gmail, regular account passwords usually will not work. Use:

1. A Google account with 2-Step Verification enabled
2. An App Password generated in Google Account security settings

Do not store your real password in public repositories.

## Web Dashboard Usage

Open the Home Node IP in your browser, for example:

```text
http://192.168.1.50/
```

The dashboard shows:

- **Global card**: overall urgency, buzzer state, buzzer silence state, home IP, WiFi channel, silence button (POST form)
- **One card per pet**: status, pet ID, MAC, RSSI, RSSI freshness, last packet age, packet counter, battery, email cooldown

The `Silence` button uses an HTML POST form so a stray prefetch or browser preview cannot trigger it. To require a shared token, set `DASHBOARD_TOKEN_VALUE` to a non-empty string in `secrets.h`; the dashboard will then render a token input field and reject submissions without the matching value.

## JSON Endpoint Example

Open:

```text
http://192.168.1.50/json
```

Example response with two pets:

```json
{
  "homeNodeIp": "192.168.1.50",
  "wifiChannel": 6,
  "overallUrgency": "Far",
  "buzzerOn": true,
  "buzzerSilenced": false,
  "pets": [
    {
      "petId": "PET-001",
      "displayName": "Bella",
      "mac": "84:F3:EB:AA:BB:CC",
      "status": "Nearby",
      "rssi": -61,
      "rssiRecent": true,
      "lastPacketAgeMs": 842,
      "packetCounter": 128,
      "batteryVoltage": 4.00,
      "emailCooldownActive": false
    },
    {
      "petId": "PET-002",
      "displayName": "Max",
      "mac": "84:F3:EB:AA:BB:CD",
      "status": "Far",
      "rssi": -82,
      "rssiRecent": true,
      "lastPacketAgeMs": 1500,
      "packetCounter": 73,
      "batteryVoltage": 3.92,
      "emailCooldownActive": true
    }
  ]
}
```

## RSSI Threshold Tuning Guide

- Start with `RSSI_THRESHOLD_DBM = -75`
- Walk with the pet node around your home
- Watch the dashboard RSSI values
- If alerts happen too early, lower the threshold to something like `-80`
- If alerts happen too late, raise the threshold to something like `-70`
- Tune based on walls, floors, and interference in your house

The same threshold currently applies to all pets. Per-pet thresholds can be added by extending `PetConfig` if you need them.

## Limitations of RSSI Tracking

- RSSI is affected by walls, body blocking, reflections, and interference
- RSSI only gives an approximate idea of distance
- Different board placement angles can change readings
- This project is best for rough proximity detection, not exact location tracking
- Filtered RSSI improves stability, but it is still not a precise distance measurement
- ESP-NOW + STA + promiscuous sniffer + WebServer all run on a single radio. Some firmware combinations are sensitive to load; if RSSI updates stall after long uptime, see [docs/troubleshooting.md](docs/troubleshooting.md).

## Future Improvements

- GPS module for outdoor location reporting
- Mobile app notification
- Battery monitoring with real ADC scaling
- Per-pet RSSI threshold and per-pet display color
- MQTT dashboard integration
- Weatherproof collar enclosure design

## Safety Note

RSSI is approximate and must not be used as a life-critical tracking method for pets, people, or property.
