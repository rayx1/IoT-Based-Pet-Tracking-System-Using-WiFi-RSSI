# Troubleshooting

## ESP-NOW init failed

- Confirm the board selected in Arduino IDE is an ESP8266 board, not ESP32.
- Make sure both sketches use the ESP8266 Arduino core.
- Reboot the board and try again.
- Check Serial Monitor for WiFi startup errors before ESP-NOW initialization.

## Pet node not detected

- Verify the Home Node MAC was copied correctly into `pet_node.ino`.
- Verify the Pet Node MAC was copied correctly into `home_node.ino`.
- Confirm both boards connect to the same WiFi router SSID.
- Check that both boards report the same WiFi channel in Serial Monitor.
- Move the boards closer together during first testing.

## RSSI not updating

- Wait for at least one valid ESP-NOW packet from the pet node.
- Confirm the home node logs show both packet reception and RSSI capture.
- Make sure `PET_NODE_MAC_BYTES` matches the Pet Node MAC exactly.
- If RSSI stays at `-127`, packets may be arriving without matching promiscuous frame capture.
- If packets arrive but RSSI does not update, temporarily test with the boards close together and confirm the Pet Node MAC filter.

## Email not sending

- Re-check `SMTP_HOST`, `SMTP_PORT`, sender email, and app password.
- Confirm your WiFi internet access is working.
- Verify the email library is installed correctly.
- Read the Serial Monitor SMTP error text.

## Gmail authentication error

- Use a Gmail App Password, not your normal account password.
- Enable 2-Step Verification on the Google account first.
- Confirm the sender email matches the account used to create the App Password.

## Web dashboard not opening

- Confirm the Home Node connected to WiFi and printed an IP address.
- Make sure your phone or laptop is on the same network.
- Open the exact IP shown in Serial Monitor.
- If needed, reboot the Home Node and try again.

## Buzzer always ON

- Increase `RSSI_THRESHOLD_DBM` because the current threshold may be too strict.
- Confirm the buzzer wiring is connected to `D5` and `GND`.
- Check if packet timeout is being triggered because the pet node is not sending.
- Check whether the dashboard silence state is active.
- Some buzzer modules are active-low; if needed, invert the buzzer logic in code.

## Wrong WiFi channel

- Both boards must lock to the same router channel.
- Make sure both connect to the same SSID before ESP-NOW exchange.
- If your router uses band steering or changing channels, test with a fixed 2.4 GHz channel.
- Reboot both nodes after router channel changes.
