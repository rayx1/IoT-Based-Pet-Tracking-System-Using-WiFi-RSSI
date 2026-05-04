# Troubleshooting

## Compile error: missing `secrets.h`

- Copy `firmware/pet_node/secrets.example.h` to `firmware/pet_node/secrets.h`.
- Copy `firmware/home_node/secrets.example.h` to `firmware/home_node/secrets.h`.
- Edit the copied files with your local WiFi, SMTP, MAC, and pet roster values.
- Do not rename `secrets.example.h`; the sketches intentionally require `secrets.h`.

## ESP-NOW init failed

- Confirm the board selected in Arduino IDE is an ESP8266 board, not ESP32.
- Make sure both sketches use the ESP8266 Arduino core.
- Reboot the board and try again.
- Check Serial Monitor for WiFi startup errors before ESP-NOW initialization.

## Pet node not detected

- Verify the Home Node MAC was copied correctly into each `pet_node` config.
- Verify each Pet Node MAC was added to `PET_NODE_LIST` on the Home Node.
- Confirm all boards connect to the same WiFi router SSID.
- Check that all boards report the same WiFi channel in Serial Monitor.
- Move the boards closer together during first testing.

## "Rejected packet ... pet ID mismatch"

- The MAC matches an entry in `PET_NODE_LIST`, but the pet node's `PET_ID_VALUE` does not equal the `PROTOCOL_PET_ID` in that entry.
- Set `PET_ID_VALUE` to the same string used in the home node entry and re-upload that pet node.

## "Ignoring packet from unknown MAC"

- A pet node is sending but its MAC is not in `PET_NODE_LIST` on the home node.
- Add an entry for that MAC, re-upload the home node, or fix the typo in the existing entry.

## RSSI not updating for one specific pet

- Confirm that pet's MAC byte sequence in `PET_NODE_LIST` exactly matches what the pet's Serial Monitor prints.
- The dashboard shows `RSSI sample: Stale` when no matching frame has been seen in the last few seconds.
- If only one of several pets shows stale RSSI while others update, the entry for that pet likely has the wrong MAC bytes.

## RSSI stalls for all pets after long uptime

- ESP-NOW, STA mode, promiscuous sniffing, and `ESP8266WebServer` share one radio.
- Try power-cycling the home node.
- If the issue is reproducible, reduce the dashboard refresh rate or increase `SEND_INTERVAL_MS_VALUE` on the pet nodes.
- Last resort: comment out `startPromiscuousSniffer()` and accept losing RSSI in exchange for stability.

## Email not sending

- Re-check `SMTP_HOST_VALUE`, `SMTP_PORT_VALUE`, sender email, and app password.
- Use implicit SSL SMTP on port `465`; STARTTLS on port `587` is not implemented by this sketch.
- Confirm your WiFi internet access is working.
- Read the Serial Monitor SMTP error text.

## Gmail authentication error

- Use a Gmail App Password, not your normal account password.
- Enable 2-Step Verification on the Google account first.
- Confirm the sender email matches the account used to create the App Password.

## Web dashboard not opening

- Confirm the Home Node connected to WiFi and printed an IP address.
- Make sure your phone or laptop is on the same network.
- Open the exact IP shown in Serial Monitor.
- Reboot the Home Node and try again if the IP is stale.

## Silence button does nothing / returns 405

- The endpoint is `POST /silence`. A plain `GET` returns `405 Use POST`.
- Use the dashboard's silence form.
- If you set `DASHBOARD_TOKEN_VALUE`, submit the matching token in the dashboard form.

## Buzzer always ON

- Increase `RSSI_THRESHOLD_DBM_VALUE` because the current threshold may be too strict.
- Confirm the buzzer wiring is connected to `D5` and `GND`.
- Check if any pet's packet timeout is being triggered.
- Check whether the dashboard silence state is active.
- Some buzzer modules are active-low; if needed, invert the logic in `writeBuzzer`.

## Buzzer pattern with multiple pets

- The buzzer reflects the most urgent pet: any `Lost` pet produces fast beeps; otherwise any `Far` pet produces slow beeps.
- A single silence press silences the buzzer for 2 minutes regardless of how many pets are alerting.

## Wrong WiFi channel

- All boards must lock to the same router channel.
- Make sure all boards connect to the same SSID before ESP-NOW exchange.
- If your router uses band steering or changing channels, test with a fixed 2.4 GHz channel.
- Reboot all nodes after router channel changes.
