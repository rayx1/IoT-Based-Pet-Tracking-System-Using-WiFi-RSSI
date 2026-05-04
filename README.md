# iot-pet-tracker-espnow-rssi

IoT pet tracker built with NodeMCU ESP8266 boards, ESP-NOW packet monitoring, buzzer alerts, SMTP-over-SSL email warnings, and a built-in web dashboard. One Home Node can track one or more Pet Nodes.

The current stable build prioritizes reliable ESP-NOW packet reception. RSSI sniffing is included as an optional experimental mode, but it is disabled by default because some ESP8266/core combinations drop ESP-NOW payload callbacks when promiscuous sniffing is enabled.

## Current Tested State

- Home Node connects to WiFi and prints IP, MAC, channel, SSID scan, and ESP-NOW setup details.
- Pet Node connects to the same WiFi channel and sends ESP-NOW packets.
- Home Node receives validated packets from Bella:

```text
Packet received from Bella (B4:8A:0A:E9:61:1E) counter=24 battery=4.00 V
Overall=Nearby, Buzzer=OFF
Bella: status=Nearby
```

- RSSI shows `-127 (stale)` when `ENABLE_RSSI_SNIFFER_VALUE` is `false`. This is expected in stable packet mode.

## Latest Debug Notes

Use this order when bringing the system up:

1. Get ESP-NOW packets working first with `ENABLE_RSSI_SNIFFER_VALUE false`.
2. Confirm Home Node prints `Packet received from Bella`.
3. Confirm both nodes are on the same WiFi channel.
4. Enable RSSI sniffer only after packet reception is stable.

If the Pet Node shows repeated `Delivery fail`, verify these first:

- Pet Node `HOME_NODE_MAC_BYTES` matches the Home Node Serial output.
- Home Node `PET_NODE_LIST` contains the Pet Node MAC.
- Both boards are connected to the same 2.4 GHz SSID/channel.
- RSSI sniffer is disabled while debugging packet delivery.

Random characters before `=== Home Node Boot ===` or `=== Pet Node Boot ===` are normal ESP8266 ROM boot text shown at a different baud rate. Keep Arduino Serial Monitor at `115200` for the project logs.

## Features

- One Home Node supervises one or more Pet Nodes.
- Pet Node sends validated ESP-NOW packets with pet ID, packet counter, uptime, and battery voltage field.
- Home Node accepts packets only from configured pet MAC addresses.
- Per-pet status: `Waiting`, `Nearby`, `Far`, `Lost`.
- Stable default mode uses packet presence:
  - `Waiting`: no valid packet received yet.
  - `Nearby`: valid packets are arriving.
  - `Lost`: packets stopped for longer than timeout.
- Optional experimental RSSI mode can classify `Far` using signal threshold.
- Buzzer reflects the most urgent state and stays OFF while pets are `Waiting` or `Nearby`.
- Per-pet SMTP-over-SSL email cooldown.
- Dashboard renders one card per pet plus a global control panel.
- JSON API at `/json`.
- Serial debug logs for WiFi, MAC addresses, IP addresses, ESP-NOW, packet counters, and recovery.
- Mandatory local `secrets.h` files keep credentials out of the repo.

## System Architecture

```text
                   Home WiFi Router
                 +------------------+
                 | 2.4 GHz channel  |
                 +---------+--------+
                           |
       channel lock        |        channel lock
                           |
      +--------------------+--------------------+
      |                                         |
      v                                         v
+------------------+    ESP-NOW packets  +----------------------+
| Pet Node         | -------------------> | Home Node            |
| NodeMCU ESP8266  |                      | NodeMCU ESP8266      |
| Pet collar       |                      | Buzzer on D5         |
+------------------+                      | Dashboard + Email    |
                                          +----------+-----------+
                                                     |
                                                     v
                                           Browser: http://IP/
```

## Hardware Required

- 1 x NodeMCU ESP8266 for the Home Node.
- 1 x NodeMCU ESP8266 per pet.
- 1 x active buzzer for Home Node.
- Jumper wires.
- USB cable for each board.
- Optional battery pack for Pet Node.

## Wiring

| Home Node Pin | Connect To | Notes |
| --- | --- | --- |
| `D5` | Active buzzer signal / `+` | Alert output |
| `GND` | Buzzer `-` / `GND` | Shared ground |
| USB / `VIN` | Power | Use stable power |

See [docs/wiring.md](docs/wiring.md).

## Software Required

- Arduino IDE 2.x or newer.
- ESP8266 Arduino core.
- Built-in ESP8266 libraries:
  - `ESP8266WiFi`
  - `ESP8266WebServer`
  - `WiFiClientSecure`
  - `espnow`

No external SMTP library is required.

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

## Quick Setup

1. Install Arduino IDE and ESP8266 board support.
2. Copy `firmware/pet_node/secrets.example.h` to `firmware/pet_node/secrets.h`.
3. Copy `firmware/home_node/secrets.example.h` to `firmware/home_node/secrets.h`.
4. Upload Pet Node once and copy its printed MAC address.
5. Put that MAC into Home Node `PET_NODE_LIST`.
6. Upload Home Node and copy its printed MAC address.
7. Put that Home Node MAC into Pet Node `HOME_NODE_MAC_BYTES`.
8. Upload Pet Node again.
9. Open Home Node IP in browser.

Detailed steps: [docs/setup.md](docs/setup.md).

## Working MAC Configuration From Test Logs

These are the MACs observed in the current test:

```cpp
// firmware/home_node/secrets.h
#define PET_NODE_LIST \
  { {0xB4, 0x8A, 0x0A, 0xE9, 0x61, 0x1E}, "PET-001", "Bella" }

// firmware/pet_node/secrets.h
#define HOME_NODE_MAC_BYTES {0x80, 0x7D, 0x3A, 0x73, 0x66, 0x03}
```

## secrets.h Workflow

All editable values live in `secrets.h`. The `.ino` files intentionally do not contain fallback credentials or duplicate config values.

Home Node config includes:

- `WIFI_SSID_VALUE`
- `WIFI_PASSWORD_VALUE`
- `SMTP_HOST_VALUE`
- `SMTP_PORT_VALUE`
- `SENDER_EMAIL_VALUE`
- `SENDER_APP_PASSWORD_VALUE`
- `RECIPIENT_EMAIL_VALUE`
- `PET_NODE_LIST`
- `DASHBOARD_TOKEN_VALUE`
- `ENABLE_RSSI_SNIFFER_VALUE`
- `ESPNOW_CHANNEL_VALUE`
- timeout/cooldown values

Pet Node config includes:

- `WIFI_SSID_VALUE`
- `WIFI_PASSWORD_VALUE`
- `PET_ID_VALUE`
- `HOME_NODE_MAC_BYTES`
- `ESPNOW_CHANNEL_VALUE`
- `SEND_INTERVAL_MS_VALUE`
- battery ADC settings

`secrets.h` is ignored by Git.

## WiFi Notes

ESP8266 supports 2.4 GHz WiFi only. The Home Node prints an initial scan like:

```text
Initial WiFi scan: nearby 2.4 GHz networks
* SSID="JioFiber-rk-4g", RSSI=-55 dBm, channel=11, BSSID=..., encryption=WPA2/CCMP
```

The configured SSID is marked with `*`.

Both nodes must lock to the same router channel. In the tested setup:

```text
Home Node WiFi channel: 11
Pet Node locked WiFi channel: 11
```

## ESP-NOW Notes

Both nodes use `ESP_NOW_ROLE_COMBO` on ESP8266 and explicitly add each other as peers.

Default:

```cpp
#define ESPNOW_CHANNEL_VALUE 0
```

`0` means use the channel obtained from WiFi. If your router channel is fixed, you may set the explicit channel, for example:

```cpp
#define ESPNOW_CHANNEL_VALUE 11
```

## RSSI Mode

Stable default:

```cpp
#define ENABLE_RSSI_SNIFFER_VALUE false
```

In this mode:

- Packet reception is reliable.
- Status changes between `Waiting`, `Nearby`, and `Lost`.
- RSSI may remain `-127 (stale)`.
- `Far` is not used unless RSSI sniffer is enabled.

Experimental RSSI mode:

```cpp
#define ENABLE_RSSI_SNIFFER_VALUE true
```

Use this only after packet reception is stable. If Home Node starts showing fresh RSSI but `Waiting` forever, turn RSSI sniffer back off.

## Web Dashboard

Open:

```text
http://HOME_NODE_IP/
```

Example from current logs:

```text
http://192.168.29.74/
```

Dashboard shows:

- Overall status.
- Buzzer state.
- Home Node IP and WiFi channel.
- One card per pet.
- Packet counter.
- Battery placeholder.
- Email cooldown.
- Buzzer silence button.

## JSON API

Open:

```text
http://HOME_NODE_IP/json
```

Example:

```json
{
  "homeNodeIp": "192.168.29.74",
  "wifiChannel": 11,
  "overallUrgency": "Nearby",
  "buzzerOn": false,
  "buzzerSilenced": false,
  "pets": [
    {
      "petId": "PET-001",
      "displayName": "Bella",
      "mac": "B4:8A:0A:E9:61:1E",
      "status": "Nearby",
      "rssi": -127,
      "rssiRecent": false,
      "lastPacketAgeMs": 169,
      "packetCounter": 28,
      "batteryVoltage": 4.00,
      "emailCooldownActive": false
    }
  ]
}
```

## Email SMTP

Email alerts use direct SMTP over SSL through `WiFiClientSecure`.

For Gmail:

- Host: `smtp.gmail.com`
- Port: `465`
- Use a Gmail App Password, not your normal account password.
- Enable 2-Step Verification before creating an App Password.

Email is skipped if placeholder SMTP values are still present.

## Serial Monitor Checklist

Healthy Home Node output:

```text
Home Node connected. IP: 192.168.29.74
Home Node MAC: 80:7D:3A:73:66:03
Home Node WiFi channel: 11
ESP-NOW initialized on Home Node.
Packet received from Bella (B4:8A:0A:E9:61:1E)
Overall=Nearby, Buzzer=OFF
```

Healthy Pet Node output:

```text
Pet Node MAC: B4:8A:0A:E9:61:1E
Pet Node connected. IP: 192.168.29.91
Locked WiFi channel: 11
Home Node peer MAC: 80:7D:3A:73:66:03
ESP-NOW initialized on pet node.
ESP-NOW send status: Delivery success
```

## Limitations

- Packet mode confirms proximity by recent packet arrival, not exact distance.
- RSSI distance is approximate and affected by walls, body blocking, antenna orientation, and interference.
- RSSI sniffing is optional and may reduce ESP-NOW reliability on some ESP8266 builds.
- No GPS location is included.
- Battery voltage is a placeholder unless ADC divider wiring is added and enabled.

## Future Improvements

- GPS module for outdoor tracking.
- Mobile app notification.
- Real battery divider and calibration.
- MQTT dashboard.
- Per-pet RSSI threshold.
- Weatherproof collar enclosure.

## Safety Note

This project is for educational proximity monitoring. RSSI and ESP-NOW packet presence must not be used as a life-critical tracking system.
