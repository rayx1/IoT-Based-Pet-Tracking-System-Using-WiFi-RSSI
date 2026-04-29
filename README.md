# iot-pet-tracker-espnow-rssi

Two-node IoT pet tracker built with NodeMCU ESP8266 boards, ESP-NOW, RSSI-based proximity detection, buzzer alerts, SMTP email warnings, and a built-in web dashboard.

## Features

- `Pet Node` sends validated ESP-NOW packets with pet ID, packet counter, uptime, and battery voltage field
- `Home Node` receives packets only from the configured Pet Node MAC
- RSSI samples are filtered by Pet Node MAC to reduce interference from unrelated WiFi traffic
- Buzzer alert uses slow beeps for `Far` and faster beeps for `Lost`
- Dashboard includes a temporary buzzer silence control
- SMTP email alert with cooldown to avoid spam
- Web dashboard on port `80`
- JSON API endpoint at `/json`
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
                           |
      +--------------------+--------------------+
      |                                         |
      v                                         v
+------------------+                    +----------------------+
| Pet Node         |   ESP-NOW packets  | Home Node            |
| NodeMCU ESP8266  | -----------------> | NodeMCU ESP8266      |
| Collar mounted   |                    | Buzzer on D5         |
| Sends pet data   |                    | Dashboard + Email    |
+------------------+                    +----------+-----------+
                                                   |
                                                   v
                                         +------------------+
                                         | User Browser     |
                                         | / and SMTP inbox |
                                         +------------------+
```

## Hardware Required

- 2 x NodeMCU ESP8266 development boards
- 1 x active buzzer
- Jumper wires
- USB cable for each board
- Optional battery pack for the pet node

## Wiring

| Home Node Pin | Connect To | Notes |
| --- | --- | --- |
| `D5` | Active buzzer signal pin | Alert output |
| `GND` | Active buzzer ground pin | Shared ground |
| `VIN` or USB | Power input | Use stable 5V USB or regulated supply |

See full notes in [docs/wiring.md](/F:/GOOGLE%20Antigravity/BCA/iot-pet-tracker-espnow-rssi/docs/wiring.md).

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
4. Upload the Pet Node once and copy its MAC from Serial Monitor.
5. Paste the Pet Node MAC into the Home Node `PET_NODE_MAC_BYTES` config.
6. Upload the Home Node and copy its MAC from Serial Monitor.
7. Paste the Home Node MAC into the Pet Node `HOME_NODE_MAC_BYTES` config.
8. Upload the Pet Node again.
9. Open the Home Node IP shown in Serial Monitor in a browser.

Detailed guide: [docs/setup.md](/F:/GOOGLE%20Antigravity/BCA/iot-pet-tracker-espnow-rssi/docs/setup.md)

## How to Find the Home Node MAC Address

1. Upload the Home Node sketch.
2. Open Serial Monitor at `115200` baud.
3. Wait for the startup logs.
4. Copy the MAC printed as `Home Node MAC`.

Example:

```text
Home Node MAC: 84:F3:EB:12:34:56
```

## How to Paste the Home Node MAC into Pet Node Code

Find this section in `pet_node.ino` or `firmware/pet_node/secrets.h`:

```cpp
#define HOME_NODE_MAC_BYTES {0x84, 0xF3, 0xEB, 0x12, 0x34, 0x56}
```

Replace the six hex values with the Home Node MAC from Serial Monitor.

## How to Paste the Pet Node MAC into Home Node Code

Find this section in `home_node.ino` or `firmware/home_node/secrets.h`:

```cpp
#define PET_NODE_MAC_BYTES {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}
```

Replace the six hex values with the Pet Node MAC from Serial Monitor. This is used for packet filtering and RSSI filtering.

## Optional secrets.h Workflow

Each firmware folder includes a `secrets.example.h`.

1. Duplicate `secrets.example.h` as `secrets.h`.
2. Edit `secrets.h` with your WiFi, SMTP, and MAC values.
3. Keep `secrets.h` private. It is already ignored by `.gitignore`.

## How to Configure Email SMTP

Edit these placeholders at the top of `home_node.ino`:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `SMTP_HOST`
- `SMTP_PORT`
- `SENDER_EMAIL`
- `SENDER_APP_PASSWORD`
- `RECIPIENT_EMAIL`

If using `secrets.h`, edit the matching `*_VALUE` macros instead.

Example for Gmail SMTP:

- Host: `smtp.gmail.com`
- Port: `465`
- Sender email: your Gmail address
- App password: 16-character Gmail App Password

## Gmail App Password Note

For Gmail, regular account passwords usually will not work. Use:

1. Google account with 2-Step Verification enabled
2. An App Password generated in Google Account security settings

Do not store your real password in public repositories.

## Web Dashboard Usage

Open the Home Node IP in your browser, for example:

```text
http://192.168.1.50/
```

The dashboard shows:

- Pet status: `Nearby`, `Far`, `Lost`, or `Waiting`
- Latest RSSI
- Last packet age
- Packet counter
- Buzzer state
- Buzzer silence state
- Email cooldown state
- Home Node IP

## JSON Endpoint Example

Open:

```text
http://192.168.1.50/json
```

Example response:

```json
{
  "petId": "PET-001",
  "status": "Nearby",
  "rssi": -61,
  "lastPacketAgeMs": 842,
  "packetCounter": 128,
  "batteryVoltage": 4.00,
  "buzzerOn": false,
  "buzzerSilenced": false,
  "emailCooldownActive": false,
  "petNodeMac": "84:F3:EB:AA:BB:CC",
  "homeNodeIp": "192.168.1.50"
}
```

## RSSI Threshold Tuning Guide

- Start with `RSSI_THRESHOLD_DBM = -75`
- Walk with the pet node around your home
- Watch the dashboard RSSI values
- If alerts happen too early, lower the threshold to something like `-80`
- If alerts happen too late, raise the threshold to something like `-70`
- Tune based on walls, floors, and interference in your house

## Limitations of RSSI Tracking

- RSSI is affected by walls, body blocking, reflections, and interference
- RSSI only gives an approximate idea of distance
- Different board placement angles can change readings
- This project is best for rough proximity detection, not exact location tracking
- Filtered RSSI improves stability, but it is still not a precise distance measurement

## Future Improvements

- GPS module for outdoor location reporting
- Mobile app notification
- Battery monitoring with real ADC scaling
- MQTT dashboard integration
- Weatherproof collar enclosure design

## Safety Note

RSSI is approximate and must not be used as a life-critical tracking method for pets, people, or property.
