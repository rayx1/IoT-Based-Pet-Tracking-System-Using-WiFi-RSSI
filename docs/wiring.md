# Wiring Guide

## Home Node Buzzer Wiring

| Home Node Pin | Buzzer Pin | Purpose |
| --- | --- | --- |
| `D5` | `SIG` / `+` | Alert signal output |
| `GND` | `GND` / `-` | Ground reference |

## Power Notes

- Power both NodeMCU boards from stable USB power during testing.
- For the pet node, use a regulated battery solution if you move to portable use.
- Avoid driving a large buzzer directly from the ESP8266 pin if the buzzer current is high.
- If your buzzer needs more current, use a transistor driver stage.
- Keep grounds common between the NodeMCU and buzzer module.
- If the pet node is battery powered, re-check WiFi strength because supply voltage can affect radio performance.

## Optional Battery Reading

The Pet Node sketch includes an ADC battery-reading option, disabled by default.

| Pet Node Pin | Connect To | Notes |
| --- | --- | --- |
| `A0` | Resistor divider output | Keep voltage within your NodeMCU A0 limit |
| `GND` | Battery negative | Shared ground |

Use a proper resistor divider and confirm your NodeMCU board's A0 input range before enabling `USE_ADC_BATTERY_READING`.
